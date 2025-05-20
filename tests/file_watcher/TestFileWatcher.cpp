#include "gtest/gtest.h"
#include "FileWatcher.h"
#include "FileUtil.h"
#include "Logger.h" // For SS_LOG_ macros (if needed for direct test logging)
#include "Error.h"  // For Errc

#include <vector>
#include <string>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <algorithm> // For std::find_if

// POSIX includes for directory manipulation if FileUtil's helpers aren't enough
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/inotify.h> // For IN_CREATE, IN_DELETE, etc.

using namespace SecureStorage::FileWatcher;
using namespace SecureStorage::Utils;
using namespace SecureStorage::Error;

class FileWatcherTest : public ::testing::Test {
protected:
    std::string testBaseDir;
    std::string currentTestDir; // Directory watched by FileWatcher in tests

    std::vector<WatchedEvent> receivedEvents;
    mutable std::mutex eventMutex; // Made mutable to allow locking in const methods like findEvent
    std::condition_variable eventCv;
    size_t expectedEventCount = 0;

    // Helper to recursively delete a directory
    void recursiveDelete(const std::string& path) {
        if (!FileUtil::pathExists(path)) return;
        DIR* dir = opendir(path.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                if (name == "." || name == "..") continue;
                std::string fullEntryPath = path + "/" + name;
                struct stat entry_stat;
                if (stat(fullEntryPath.c_str(), &entry_stat) == 0) {
                    if (S_ISDIR(entry_stat.st_mode)) {
                        recursiveDelete(fullEntryPath);
                    } else {
                        std::remove(fullEntryPath.c_str());
                    }
                }
            }
            closedir(dir);
        }
        std::remove(path.c_str());
    }

    void SetUp() override {
        #ifdef CMAKE_BINARY_DIR
            testBaseDir = std::string(CMAKE_BINARY_DIR) + "/FileWatcherTests_temp";
        #else
            char* temp_env = std::getenv("TMPDIR");
            if (temp_env) testBaseDir = std::string(temp_env);
            else temp_env = std::getenv("TEMP");
            if (temp_env) testBaseDir = std::string(temp_env);
            else testBaseDir = ".";
            testBaseDir += "/FileWatcherTests_temp";
        #endif

        FileUtil::createDirectories(testBaseDir);

        std::ostringstream oss;
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        oss << testBaseDir << "/watch_dir_" << std::this_thread::get_id() << "_" << now_ms;
        currentTestDir = oss.str();

        recursiveDelete(currentTestDir);
        ASSERT_EQ(FileUtil::createDirectories(currentTestDir), Errc::Success)
            << "Failed to create temporary watch directory: " << currentTestDir;
        SS_LOG_INFO("FileWatcherTest: Using temporary watch directory: " << currentTestDir);
        
        receivedEvents.clear();
        expectedEventCount = 0;
    }

    void TearDown() override {
        SS_LOG_INFO("FileWatcherTest: Cleaning up temporary watch directory: " << currentTestDir);
        recursiveDelete(currentTestDir);
    }

    EventCallback getTestCallback() {
        return [this](const WatchedEvent& event) {
            std::lock_guard<std::mutex> lock(this->eventMutex);
            this->receivedEvents.push_back(event);
            SS_LOG_DEBUG("TestCallback: Received event for path='" << event.filePath 
                         << "', name='" << event.fileName 
                         << "', mask=0x" << std::hex << event.mask << std::dec);
            if (this->receivedEvents.size() >= this->expectedEventCount) {
                this->eventCv.notify_one();
            }
        };
    }

    // Waits for a specific number of events or until timeout
    bool waitForEvents(size_t count, std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
        expectedEventCount = count;
        std::unique_lock<std::mutex> lock(eventMutex);
        return eventCv.wait_for(lock, timeout, [this, count] { return this->receivedEvents.size() >= count; });
    }
    
    // Helper to find a specific event
    const WatchedEvent* findEvent(uint32_t targetMask, const std::string& targetName = "") const {
        std::lock_guard<std::mutex> lock(eventMutex); // Ensure thread safety for receivedEvents access
        auto it = std::find_if(receivedEvents.begin(), receivedEvents.end(),
            [&](const WatchedEvent& ev) {
                bool maskMatch = (ev.mask & targetMask) != 0;
                bool nameMatch = targetName.empty() ? true : (ev.fileName == targetName);
                return maskMatch && nameMatch;
            });
        return (it == receivedEvents.end()) ? nullptr : &(*it);
    }
};


TEST_F(FileWatcherTest, StartAndStop) {
    FileWatcher watcher(getTestCallback());
    ASSERT_TRUE(watcher.start());
    // Give the thread a moment to fully start (not strictly necessary but good for some environments)
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); 
    watcher.stop(); // This blocks until thread joins
    ASSERT_FALSE(watcher.start()); // Should not restart if not designed to
                                  // Current design: stop cleans up FDs, so start would re-init
                                  // Let's assume stop() makes it fully unusable without re-init.
                                  // A better test might try to add a watch after stop to ensure it fails.
}

TEST_F(FileWatcherTest, AddWatchToExistingDirectory) {
    FileWatcher watcher(getTestCallback());
    ASSERT_TRUE(watcher.start());
    ASSERT_TRUE(watcher.addWatch(currentTestDir));
    watcher.stop();
}

TEST_F(FileWatcherTest, AddWatchToExistingFile) {
    FileWatcher watcher(getTestCallback());
    ASSERT_TRUE(watcher.start());
    std::string testFile = currentTestDir + "/testfile.txt";
    std::ofstream ofs(testFile);
    ofs << "hello";
    ofs.close();
    ASSERT_TRUE(FileUtil::pathExists(testFile));
    
    ASSERT_TRUE(watcher.addWatch(testFile));
    watcher.stop();
}

TEST_F(FileWatcherTest, AddWatchToNonExistentPathFails) {
    FileWatcher watcher(getTestCallback());
    ASSERT_TRUE(watcher.start());
    std::string nonExistentFile = currentTestDir + "/idontexist.txt";
    // inotify_add_watch fails if the path does not exist
    ASSERT_FALSE(watcher.addWatch(nonExistentFile)); 
    watcher.stop();
}

TEST_F(FileWatcherTest, RemoveWatch) {
    FileWatcher watcher(getTestCallback());
    ASSERT_TRUE(watcher.start());
    ASSERT_TRUE(watcher.addWatch(currentTestDir));
    ASSERT_TRUE(watcher.removeWatch(currentTestDir));
    ASSERT_FALSE(watcher.removeWatch(currentTestDir)); // Already removed
    watcher.stop();
}

TEST_F(FileWatcherTest, FileCreateInWatchedDirectory) {
    FileWatcher watcher(getTestCallback());
    ASSERT_TRUE(watcher.start());
    ASSERT_TRUE(watcher.addWatch(currentTestDir));

    std::string newFile = currentTestDir + "/newfile.txt";
    std::ofstream ofs(newFile);
    ofs << "content";
    ofs.close(); // This should trigger IN_CREATE, IN_MODIFY, and IN_CLOSE_WRITE

    // Expect CREATE, MODIFY, and CLOSE_WRITE.
    // Wait for all 3 events based on typical observed behavior.
    ASSERT_TRUE(waitForEvents(3, std::chrono::seconds(1))) << "Timed out waiting for 3 events (CREATE, MODIFY, CLOSE_WRITE).";
    
    const WatchedEvent* createEvent = findEvent(IN_CREATE, "newfile.txt");
    ASSERT_NE(createEvent, nullptr) << "IN_CREATE event for newfile.txt not found.";
    if(createEvent) {
        EXPECT_EQ(createEvent->filePath, currentTestDir);
    }

    // Also verify IN_MODIFY since the log shows it occurs
    const WatchedEvent* modifyEvent = findEvent(IN_MODIFY, "newfile.txt");
    ASSERT_NE(modifyEvent, nullptr) << "IN_MODIFY event for newfile.txt not found.";
    if(modifyEvent) {
        EXPECT_EQ(modifyEvent->filePath, currentTestDir);
    }

    const WatchedEvent* closeWriteEvent = findEvent(IN_CLOSE_WRITE, "newfile.txt");
    ASSERT_NE(closeWriteEvent, nullptr) << "IN_CLOSE_WRITE event for newfile.txt not found."; // This was failing
    if(closeWriteEvent) {
        EXPECT_EQ(closeWriteEvent->filePath, currentTestDir);
    }

    watcher.stop();
}

TEST_F(FileWatcherTest, FileModifyInWatchedDirectory) {
    std::string testFile = currentTestDir + "/modify_me.txt";
    std::ofstream ofs(testFile);
    ofs << "initial content";
    ofs.close();

    FileWatcher watcher(getTestCallback());
    ASSERT_TRUE(watcher.start());
    ASSERT_TRUE(watcher.addWatch(currentTestDir)); // Watch the directory containing the file

    // Modify the file
    std::ofstream ofs_modify(testFile, std::ios::app);
    ofs_modify << " appended content";
    ofs_modify.close(); // Triggers IN_MODIFY, IN_CLOSE_WRITE

    ASSERT_TRUE(waitForEvents(2, std::chrono::seconds(1))); // Expect MODIFY and CLOSE_WRITE

    const WatchedEvent* modifyEvent = findEvent(IN_MODIFY, "modify_me.txt");
    ASSERT_NE(modifyEvent, nullptr);
    EXPECT_EQ(modifyEvent->filePath, currentTestDir);
    
    const WatchedEvent* closeWriteEvent = findEvent(IN_CLOSE_WRITE, "modify_me.txt");
    ASSERT_NE(closeWriteEvent, nullptr);
    EXPECT_EQ(closeWriteEvent->filePath, currentTestDir);

    watcher.stop();
}


TEST_F(FileWatcherTest, FileDeleteFromWatchedDirectory) {
    std::string fileToDelete = currentTestDir + "/delete_me.txt";
    std::ofstream(fileToDelete) << "content"; // Create the file

    FileWatcher watcher(getTestCallback());
    ASSERT_TRUE(watcher.start());
    ASSERT_TRUE(watcher.addWatch(currentTestDir));

    std::remove(fileToDelete.c_str()); // Delete the file

    ASSERT_TRUE(waitForEvents(1, std::chrono::seconds(1))); // Expect IN_DELETE

    const WatchedEvent* deleteEvent = findEvent(IN_DELETE, "delete_me.txt");
    ASSERT_NE(deleteEvent, nullptr);
    EXPECT_EQ(deleteEvent->filePath, currentTestDir);

    watcher.stop();
}

TEST_F(FileWatcherTest, WatchedFileDeleteSelf) {
    std::string fileToWatchAndDel = currentTestDir + "/watch_and_delete_me.txt";
    std::ofstream(fileToWatchAndDel) << "temporary";

    FileWatcher watcher(getTestCallback());
    ASSERT_TRUE(watcher.start());
    ASSERT_TRUE(watcher.addWatch(fileToWatchAndDel)); // Watch the file itself

    std::remove(fileToWatchAndDel.c_str()); // Delete the file

    // Expect IN_ATTRIB, IN_DELETE_SELF, and then IN_IGNORED for the specific watch.
    // Wait for all 3 events based on typical observed behavior.
    ASSERT_TRUE(waitForEvents(3, std::chrono::seconds(1))) << "Timed out waiting for 3 events.";

    // Verify IN_DELETE_SELF
    const WatchedEvent* deleteSelfEvent = findEvent(IN_DELETE_SELF);
    ASSERT_NE(deleteSelfEvent, nullptr) << "IN_DELETE_SELF event not found.";
    if (deleteSelfEvent) { // Check to avoid dereferencing nullptr if previous assert fails with EXPECT_NE
        EXPECT_EQ(deleteSelfEvent->filePath, fileToWatchAndDel);
        EXPECT_TRUE(deleteSelfEvent->fileName.empty()); // fileName not applicable for SELF events on files
    }

    // Verify IN_IGNORED
    const WatchedEvent* ignoredEvent = findEvent(IN_IGNORED);
    ASSERT_NE(ignoredEvent, nullptr) << "IN_IGNORED event not found.";
    if (ignoredEvent) {
        EXPECT_EQ(ignoredEvent->filePath, fileToWatchAndDel);
    }
    
    // Optionally verify IN_ATTRIB if it's consistently the first one
    // const WatchedEvent* attribEvent = findEvent(IN_ATTRIB);
    // ASSERT_NE(attribEvent, nullptr) << "IN_ATTRIB event not found.";

    watcher.stop();
}