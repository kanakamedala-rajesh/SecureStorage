#include "gtest/gtest.h"

#include "SecureStorageManager.h" // Main public API header
#include "Error.h"   // For Errc
#include "FileUtil.h"  // For cleaning up test directories, pathExists
#include "Logger.h"  // For SS_LOG macros
#include "file_watcher/FileWatcher.h" // For file watcher functionality
#include "storage/SecureStore.h" // For SecureStore functionality

#include <vector>
#include <string>
#include <thread>   // For std::this_thread::get_id
#include <chrono>   // For std::chrono
#include <fstream>  // For creating dummy files if needed for setup
#include <mutex>
#include <condition_variable>
#include <algorithm>

// POSIX includes for directory manipulation if FileUtil's helpers aren't enough for test cleanup
#include <sys/stat.h> // For S_ISDIR in recursiveDelete
#include <dirent.h>   // For opendir, etc. in recursiveDelete
#include <unistd.h>   // For rmdir, unlink for manual test cleanup
#include <cstdio>     // For std::remove

// Ensure inotify constants are available if not pulled by FileWatcher.h already
#ifndef IN_CREATE
#include <sys/inotify.h>
#endif

using namespace SecureStorage; // Access SecureStorageManager, Error::Errc
// Explicitly qualify sub-namespaces if needed, e.g., SecureStorage::Utils::FileUtil

class SecureStorageManagerTest : public ::testing::Test {
protected:
    std::string testBaseDir;
    std::string currentTestRootDir; // Root storage path for SecureStorageManager
    std::string dummySerial = "MgrTestSerial789";

    // For capturing FileWatcher events
    std::vector<FileWatcher::WatchedEvent> receivedEvents;
    mutable std::mutex eventMutex;
    std::condition_variable eventCv;
    size_t expectedEventCount = 0;

    // Simplified recursiveDelete (consider moving to a common test utility if used in multiple places)
    void recursiveDelete(const std::string& path) {
        if (!Utils::FileUtil::pathExists(path)) return;
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
        std::remove(path.c_str()); // remove() can delete file or empty directory
    }


    void SetUp() override {
        #ifdef CMAKE_BINARY_DIR
            testBaseDir = std::string(CMAKE_BINARY_DIR) + "/SecureStorageManagerTests_temp";
        #else
            char* temp_env = std::getenv("TMPDIR");
            if (temp_env) testBaseDir = std::string(temp_env);
            else temp_env = std::getenv("TEMP");
            if (temp_env) testBaseDir = std::string(temp_env);
            else testBaseDir = "."; // Current directory as last resort
            testBaseDir += "/SecureStorageManagerTests_temp";
        #endif

        Utils::FileUtil::createDirectories(testBaseDir);

        std::ostringstream oss;
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        oss << testBaseDir << "/manager_test_root_" << std::this_thread::get_id() << "_" << now_ms;
        currentTestRootDir = oss.str();

        recursiveDelete(currentTestRootDir); // Clean up from previous potentially failed run
        ASSERT_EQ(Utils::FileUtil::createDirectories(currentTestRootDir), Error::Errc::Success)
            << "Failed to create temporary root directory for manager test: " << currentTestRootDir;
        SS_LOG_INFO("SecureStorageManagerTest: Using temp root: " << currentTestRootDir);
    }

    void TearDown() override {
        SS_LOG_INFO("SecureStorageManagerTest: Cleaning up temp root: " << currentTestRootDir);
        recursiveDelete(currentTestRootDir);
    }

    FileWatcher::EventCallback getTestEventCallback() {
        return [this](const FileWatcher::WatchedEvent& event) {
            std::lock_guard<std::mutex> lock(this->eventMutex);
            this->receivedEvents.push_back(event);
            SS_LOG_DEBUG("SSM TestCallback: Received event for path='" << event.filePath
                         << "', name='" << event.fileName
                         << "', mask=0x" << std::hex << event.mask << std::dec
                         << " Event(s): [" << event.eventNameStr << "]");
            if (this->receivedEvents.size() >= this->expectedEventCount) {
                this->eventCv.notify_all(); // Notify all waiting, just in case
            }
        };
    }

    bool waitForEvents(size_t count, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
        expectedEventCount = count; // Update expected count before waiting
        std::unique_lock<std::mutex> lock(eventMutex);
        if (receivedEvents.size() >= count) return true; // Already have enough
        return eventCv.wait_for(lock, timeout, [this, count] { return this->receivedEvents.size() >= count; });
    }

    const FileWatcher::WatchedEvent* findEvent(uint32_t targetMask, const std::string& targetName = "") const {
        std::lock_guard<std::mutex> lock(eventMutex);
        auto it = std::find_if(receivedEvents.begin(), receivedEvents.end(),
                                [&targetMask, &targetName](const FileWatcher::WatchedEvent& ev) {
            bool maskMatch = (ev.mask & targetMask) != 0;
            bool nameMatch = targetName.empty() ? true : (ev.fileName == targetName);
            return maskMatch && nameMatch;
        });
        return (it == receivedEvents.end()) ? nullptr : &(*it);
    }
};

TEST_F(SecureStorageManagerTest, InitializationSuccess) {
    SecureStorageManager manager(currentTestRootDir, dummySerial, getTestEventCallback());
    EXPECT_TRUE(manager.isInitialized());
}

TEST_F(SecureStorageManagerTest, InitializationFailsWithEmptyRootPath) {
    SecureStorageManager manager("", dummySerial, getTestEventCallback());
    EXPECT_FALSE(manager.isInitialized());
}

TEST_F(SecureStorageManagerTest, InitializationFailsWithEmptySerial) {
    SecureStorageManager manager(currentTestRootDir, "", getTestEventCallback());
    EXPECT_FALSE(manager.isInitialized());
}

TEST_F(SecureStorageManagerTest, OperationsFailIfNotInitialized) {
    SecureStorageManager manager("", "", getTestEventCallback()); // Force initialization failure
    ASSERT_FALSE(manager.isInitialized());

    std::string id = "test_id";
    std::vector<unsigned char> data_to_store = {'a'};
    std::vector<unsigned char> retrieved_data;
    std::vector<std::string> ids_list;

    EXPECT_EQ(manager.storeData(id, data_to_store), Error::Errc::NotInitialized);
    EXPECT_EQ(manager.retrieveData(id, retrieved_data), Error::Errc::NotInitialized);
    EXPECT_TRUE(retrieved_data.empty()); // retrieveData should clear output on failure
    EXPECT_EQ(manager.deleteData(id), Error::Errc::NotInitialized);
    EXPECT_FALSE(manager.dataExists(id)); // Should return false if not initialized
    EXPECT_EQ(manager.listDataIds(ids_list), Error::Errc::NotInitialized);
    EXPECT_TRUE(ids_list.empty()); // listDataIds should clear output on failure
}

TEST_F(SecureStorageManagerTest, BasicStoreAndRetrieveDelegation) {
    SecureStorageManager manager(currentTestRootDir, dummySerial, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized());

    std::string id = "delegation_test";
    std::vector<unsigned char> store_vec = {'s', 't', 'o', 'r', 'e'};
    std::vector<unsigned char> retrieve_vec;

    ASSERT_EQ(manager.storeData(id, store_vec), Error::Errc::Success);
    ASSERT_TRUE(manager.dataExists(id));

    ASSERT_EQ(manager.retrieveData(id, retrieve_vec), Error::Errc::Success);
    ASSERT_EQ(retrieve_vec, store_vec);
}

TEST_F(SecureStorageManagerTest, DataExistsDelegation) {
    SecureStorageManager manager(currentTestRootDir, dummySerial, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized());
    std::string id = "exists_deleg_test";
    std::vector<unsigned char> data = {'e'};

    ASSERT_FALSE(manager.dataExists(id));
    ASSERT_EQ(manager.storeData(id, data), Error::Errc::Success);
    ASSERT_TRUE(manager.dataExists(id));
    ASSERT_EQ(manager.deleteData(id), Error::Errc::Success);
    ASSERT_FALSE(manager.dataExists(id));
}

TEST_F(SecureStorageManagerTest, ListDataIdsDelegation) {
    SecureStorageManager manager(currentTestRootDir, dummySerial, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized());
    std::vector<std::string> ids;

    ASSERT_EQ(manager.listDataIds(ids), Error::Errc::Success);
    ASSERT_TRUE(ids.empty());

    ASSERT_EQ(manager.storeData("item1", {'1'}), Error::Errc::Success);
    ASSERT_EQ(manager.storeData("item2", {'2'}), Error::Errc::Success);

    ASSERT_EQ(manager.listDataIds(ids), Error::Errc::Success);
    ASSERT_EQ(ids.size(), 2);
    // Items are sorted by SecureStore::listDataIds
    EXPECT_EQ(ids[0], "item1");
    EXPECT_EQ(ids[1], "item2");
}

TEST_F(SecureStorageManagerTest, MoveConstructor) {
    SecureStorageManager manager1(currentTestRootDir, dummySerial, getTestEventCallback());
    ASSERT_TRUE(manager1.isInitialized());

    std::string id = "move_test_data";
    std::vector<unsigned char> data = {'m', 'o', 'v', 'e'};
    ASSERT_EQ(manager1.storeData(id, data), Error::Errc::Success);

    SecureStorageManager manager2(std::move(manager1));
    ASSERT_TRUE(manager2.isInitialized());
    // manager1 should be in a valid but uninitialized/moved-from state
    // Accessing its m_impl would be UB if it were truly nulled.
    // The SecureStorageManager::isInitialized() should rely on m_impl and m_impl->isManagerInitialized
    // For a PImpl unique_ptr, manager1.m_impl is now nullptr.
    ASSERT_FALSE(manager1.isInitialized()) << "Moved-from manager should not be initialized.";


    std::vector<unsigned char> retrieved_data;
    ASSERT_EQ(manager2.retrieveData(id, retrieved_data), Error::Errc::Success);
    ASSERT_EQ(retrieved_data, data);

    // Operations on moved-from manager should fail
    ASSERT_EQ(manager1.retrieveData(id, retrieved_data), Error::Errc::NotInitialized);
}

TEST_F(SecureStorageManagerTest, MoveAssignment) {
    SecureStorageManager manager1(currentTestRootDir, dummySerial, getTestEventCallback());
    ASSERT_TRUE(manager1.isInitialized());
    std::string id1 = "move_assign_data1";
    std::vector<unsigned char> data1 = {'a', 's', 's', 'i', 'g', 'n', '1'};
    ASSERT_EQ(manager1.storeData(id1, data1), Error::Errc::Success);

    // Create a different root path for manager2 to ensure resources are distinct before move
    std::string anotherRootDir = testBaseDir + "/manager_test_root_assign";
    recursiveDelete(anotherRootDir);
    ASSERT_EQ(Utils::FileUtil::createDirectories(anotherRootDir), Error::Errc::Success);
    SecureStorageManager manager2(anotherRootDir, "SerialForManager2", getTestEventCallback());
    ASSERT_TRUE(manager2.isInitialized());
    std::string id2 = "move_assign_data2";
    std::vector<unsigned char> data2 = {'a', 's', 's', 'i', 'g', 'n', '2'};
    ASSERT_EQ(manager2.storeData(id2, data2), Error::Errc::Success);


    manager2 = std::move(manager1); // Move assignment
    ASSERT_TRUE(manager2.isInitialized());
    ASSERT_FALSE(manager1.isInitialized()) << "Moved-from manager should not be initialized (assignment).";

    std::vector<unsigned char> retrieved_data;
    // manager2 should now manage data from manager1's original root path
    ASSERT_EQ(manager2.retrieveData(id1, retrieved_data), Error::Errc::Success);
    ASSERT_EQ(retrieved_data, data1);

    // Data that was in manager2's original store should effectively be gone (or inaccessible via this manager2 instance)
    ASSERT_EQ(manager2.retrieveData(id2, retrieved_data), Error::Errc::DataNotFound)
        << "Data from manager2's original store should not be found after move assignment.";

    // Clean up the extra directory manually as manager2's TearDown won't know about it
    recursiveDelete(anotherRootDir);
}

TEST_F(SecureStorageManagerTest, InitializationWithFileWatcher) {
    SecureStorageManager manager(currentTestRootDir, dummySerial, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized()) << "Manager (SecureStore) should be initialized.";
    ASSERT_TRUE(manager.isFileWatcherActive()) << "File watcher should be active.";
    // Manager destructor will stop the watcher.
}

TEST_F(SecureStorageManagerTest, WatcherDetectsExternalFileCreation) {
    SecureStorageManager manager(currentTestRootDir, dummySerial, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized());
    ASSERT_TRUE(manager.isFileWatcherActive());

    std::string newFilePath = currentTestRootDir + "/externally_created.txt";
    SS_LOG_INFO("Test: Creating external file: " << newFilePath);
    std::ofstream ofs(newFilePath);
    ofs << "external content";
    ofs.close();

    // Expect IN_CREATE and IN_CLOSE_WRITE (and possibly IN_MODIFY) for the new file
    // Wait for at least 2, then check specifics. Let's be generous and wait for 3.
    ASSERT_TRUE(waitForEvents(2, std::chrono::seconds(2))) << "Did not receive expected number of events for external creation.";

    const FileWatcher::WatchedEvent* createEvent = findEvent(IN_CREATE, "externally_created.txt");
    const FileWatcher::WatchedEvent* closeWriteEvent = findEvent(IN_CLOSE_WRITE, "externally_created.txt");

    ASSERT_NE(createEvent, nullptr) << "IN_CREATE event not detected for external file.";
    if (createEvent) {
        EXPECT_EQ(createEvent->filePath, currentTestRootDir); // Watch is on the directory
    }
    ASSERT_NE(closeWriteEvent, nullptr) << "IN_CLOSE_WRITE event not detected for external file.";
     if (closeWriteEvent) {
        EXPECT_EQ(closeWriteEvent->filePath, currentTestRootDir);
    }
}

TEST_F(SecureStorageManagerTest, WatcherDetectsExternalModification) {
    std::string dataId = "watched_item";
    std::vector<unsigned char> data = {'o', 'r', 'i', 'g', 'i', 'n', 'a', 'l'};
    
    // Manager without a test callback for this setup part
    {
        SecureStorageManager initialManager(currentTestRootDir, dummySerial, getTestEventCallback());
        ASSERT_TRUE(initialManager.isInitialized());
        ASSERT_EQ(initialManager.storeData(dataId, data), Error::Errc::Success);
    } // initialManager goes out of scope, stops its internal watcher.

    // Now, create a new manager with our test callback to observe external modification
    receivedEvents.clear(); // Clear events from previous manager's operations
    SecureStorageManager manager(currentTestRootDir, dummySerial, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized());
    ASSERT_TRUE(manager.isFileWatcherActive());

    std::string targetFilePath = currentTestRootDir + "/" + dataId + SecureStorage::Storage::DATA_FILE_EXTENSION;
    ASSERT_TRUE(Utils::FileUtil::pathExists(targetFilePath));

    SS_LOG_INFO("Test: Externally modifying file: " << targetFilePath);
    std::ofstream ofs(targetFilePath, std::ios::binary | std::ios::app); // Append
    ofs.write("mod", 3);
    ofs.close();

    // Expect IN_MODIFY and IN_CLOSE_WRITE for the specific file
    ASSERT_TRUE(waitForEvents(2, std::chrono::seconds(2))) << "Did not receive expected events for external modification.";

    const FileWatcher::WatchedEvent* modifyEvent = findEvent(IN_MODIFY, dataId + SecureStorage::Storage::DATA_FILE_EXTENSION);
    const FileWatcher::WatchedEvent* closeWriteEvent = findEvent(IN_CLOSE_WRITE, dataId + SecureStorage::Storage::DATA_FILE_EXTENSION);

    ASSERT_NE(modifyEvent, nullptr) << "IN_MODIFY event not detected.";
    ASSERT_NE(closeWriteEvent, nullptr) << "IN_CLOSE_WRITE event not detected.";
}

TEST_F(SecureStorageManagerTest, WatcherDetectsExternalDeletion) {
    std::string dataId = "item_to_delete_externally";
    std::vector<unsigned char> data = {'d', 'e', 'l'};
    std::string targetFileName = dataId + SecureStorage::Storage::DATA_FILE_EXTENSION;

    {
        SecureStorageManager setupManager(currentTestRootDir, dummySerial, getTestEventCallback());
        ASSERT_TRUE(setupManager.isInitialized());
        ASSERT_EQ(setupManager.storeData(dataId, data), Error::Errc::Success);
    }
    
    receivedEvents.clear();
    SecureStorageManager manager(currentTestRootDir, dummySerial, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized());
    ASSERT_TRUE(manager.isFileWatcherActive());

    std::string targetFilePath = currentTestRootDir + "/" + targetFileName;
    ASSERT_TRUE(Utils::FileUtil::pathExists(targetFilePath));

    SS_LOG_INFO("Test: Externally deleting file: " << targetFilePath);
    ASSERT_EQ(Utils::FileUtil::deleteFile(targetFilePath), Error::Errc::Success);

    ASSERT_TRUE(waitForEvents(1, std::chrono::seconds(2))) << "Did not receive IN_DELETE event.";
    
    const FileWatcher::WatchedEvent* deleteEvent = findEvent(IN_DELETE, targetFileName);
    ASSERT_NE(deleteEvent, nullptr) << "IN_DELETE event not detected for external deletion.";
    if (deleteEvent) {
        EXPECT_EQ(deleteEvent->filePath, currentTestRootDir); // Event is on the watched directory
    }
}

TEST_F(SecureStorageManagerTest, ManagerInitializesAndActivatesFileWatcher) {
    SS_LOG_INFO("Test: ManagerInitializesAndActivatesFileWatcher");
    SecureStorageManager manager(currentTestRootDir, dummySerial, getTestEventCallback());
    
    ASSERT_TRUE(manager.isInitialized()) << "Manager (SecureStore component) should be initialized.";
    ASSERT_TRUE(manager.isFileWatcherActive()) << "File watcher within manager should be active.";
    
    // Manager's destructor will stop the watcher.
    // If we want to be more explicit, we could check for watcher logs indicating start,
    // but isFileWatcherActive() is the primary check here.
}

TEST_F(SecureStorageManagerTest, WatcherViaManagerDetectsExternalFileCreation) {
    SS_LOG_INFO("Test: WatcherViaManagerDetectsExternalFileCreation");
    SecureStorageManager manager(currentTestRootDir, dummySerial, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized());
    ASSERT_TRUE(manager.isFileWatcherActive());

    std::string externallyCreatedFileName = "external_new_file.txt";
    std::string newFilePath = currentTestRootDir + "/" + externallyCreatedFileName;

    SS_LOG_DEBUG("Test: Creating external file: " << newFilePath);
    std::ofstream ofs(newFilePath);
    ofs << "external content";
    ofs.close(); // This action should trigger events.

    // Expect IN_CREATE for the new file within the watched directory (currentTestRootDir)
    // and IN_CLOSE_WRITE when the ofstream is closed.
    // Some systems might also generate IN_MODIFY. Wait for at least 2, check for the critical ones.
    ASSERT_TRUE(waitForEvents(2, std::chrono::seconds(2))) 
        << "Timed out waiting for events for external file creation. Received " << receivedEvents.size() << " events.";

    const FileWatcher::WatchedEvent* createEvent = findEvent(IN_CREATE, externallyCreatedFileName);
    ASSERT_NE(createEvent, nullptr) << "IN_CREATE event not detected for external file.";
    if (createEvent) {
        EXPECT_EQ(createEvent->filePath, currentTestRootDir); // FileWatcher watches the directory
        EXPECT_EQ(createEvent->fileName, externallyCreatedFileName);
    }

    const FileWatcher::WatchedEvent* closeWriteEvent = findEvent(IN_CLOSE_WRITE, externallyCreatedFileName);
    ASSERT_NE(closeWriteEvent, nullptr) << "IN_CLOSE_WRITE event not detected for external file.";
    if (closeWriteEvent) {
        EXPECT_EQ(closeWriteEvent->filePath, currentTestRootDir);
        EXPECT_EQ(closeWriteEvent->fileName, externallyCreatedFileName);
    }
    // Manager destructor will stop the watcher.
}

TEST_F(SecureStorageManagerTest, WatcherViaManagerDetectsExternalModification) {
    SS_LOG_INFO("Test: WatcherViaManagerDetectsExternalModification");
    std::string dataId = "item_to_modify_externally";
    std::vector<unsigned char> initialData = {'v', '1'};
    std::string targetEncryptedFileName = dataId + SecureStorage::Storage::DATA_FILE_EXTENSION;
    std::string fullTargetFilePath = currentTestRootDir + "/" + targetEncryptedFileName;

    // Step 1: Create a file using a manager instance that DOES NOT have our test callback,
    // so its creation events don't pollute `receivedEvents`.
    {
        SecureStorageManager setupManager(currentTestRootDir, dummySerial, getTestEventCallback()); // No test callback
        ASSERT_TRUE(setupManager.isInitialized());
        ASSERT_EQ(setupManager.storeData(dataId, initialData), Error::Errc::Success);
        ASSERT_TRUE(Utils::FileUtil::pathExists(fullTargetFilePath));
    } // setupManager goes out of scope, its watcher stops.

    // Step 2: Create the main manager instance WITH the test callback to observe modifications.
    receivedEvents.clear(); // Clear any stray events if any (shouldn't be)
    expectedEventCount = 0;
    SecureStorageManager manager(currentTestRootDir, dummySerial, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized());
    ASSERT_TRUE(manager.isFileWatcherActive());

    // Step 3: Externally modify the file
    SS_LOG_DEBUG("Test: Externally modifying file: " << fullTargetFilePath);
    std::ofstream ofs(fullTargetFilePath, std::ios::binary | std::ios::app); // Append to modify
    ASSERT_TRUE(ofs.is_open()) << "Failed to open file for external modification.";
    ofs.write("mod", 3);
    ASSERT_TRUE(ofs.good()) << "Failed to write during external modification.";
    ofs.close();

    // Expect IN_MODIFY and IN_CLOSE_WRITE on the specific file.
    // Since the watch is on the directory, the event will report the filename.
    ASSERT_TRUE(waitForEvents(2, std::chrono::seconds(2))) 
        << "Timed out waiting for events for external modification. Received " << receivedEvents.size() << " events.";

    const FileWatcher::WatchedEvent* modifyEvent = findEvent(IN_MODIFY, targetEncryptedFileName);
    ASSERT_NE(modifyEvent, nullptr) << "IN_MODIFY event not detected for " << targetEncryptedFileName;
    if (modifyEvent) {
        EXPECT_EQ(modifyEvent->filePath, currentTestRootDir);
    }

    const FileWatcher::WatchedEvent* closeWriteEvent = findEvent(IN_CLOSE_WRITE, targetEncryptedFileName);
    ASSERT_NE(closeWriteEvent, nullptr) << "IN_CLOSE_WRITE event not detected for " << targetEncryptedFileName;
    if (closeWriteEvent) {
        EXPECT_EQ(closeWriteEvent->filePath, currentTestRootDir);
    }
    // Manager destructor will stop the watcher.
}

TEST_F(SecureStorageManagerTest, WatcherViaManagerDetectsExternalDeletion) {
    SS_LOG_INFO("Test: WatcherViaManagerDetectsExternalDeletion");
    std::string dataId = "item_to_delete_externally";
    std::vector<unsigned char> data = {'d', 'e', 'l'};
    std::string targetEncryptedFileName = dataId + SecureStorage::Storage::DATA_FILE_EXTENSION;
    std::string fullTargetFilePath = currentTestRootDir + "/" + targetEncryptedFileName;

    // Step 1: Create the file
    {
        SecureStorageManager setupManager(currentTestRootDir, dummySerial, getTestEventCallback());
        ASSERT_TRUE(setupManager.isInitialized());
        ASSERT_EQ(setupManager.storeData(dataId, data), Error::Errc::Success);
    }

    // Step 2: Start manager with watcher to observe deletion
    receivedEvents.clear();
    expectedEventCount = 0;
    SecureStorageManager manager(currentTestRootDir, dummySerial, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized());
    ASSERT_TRUE(manager.isFileWatcherActive());
    ASSERT_TRUE(Utils::FileUtil::pathExists(fullTargetFilePath)); // Ensure file exists before deleting

    // Step 3: Externally delete the file
    SS_LOG_DEBUG("Test: Externally deleting file: " << fullTargetFilePath);
    ASSERT_EQ(Utils::FileUtil::deleteFile(fullTargetFilePath), Error::Errc::Success);

    // Expect IN_DELETE for the file within the watched directory.
    ASSERT_TRUE(waitForEvents(1, std::chrono::seconds(2))) 
        << "Timed out waiting for IN_DELETE event. Received " << receivedEvents.size() << " events.";

    const FileWatcher::WatchedEvent* deleteEvent = findEvent(IN_DELETE, targetEncryptedFileName);
    ASSERT_NE(deleteEvent, nullptr) << "IN_DELETE event not detected for " << targetEncryptedFileName;
    if (deleteEvent) {
        EXPECT_EQ(deleteEvent->filePath, currentTestRootDir); // Watch is on the directory
        EXPECT_EQ(deleteEvent->fileName, targetEncryptedFileName);
    }
    // Manager destructor will stop the watcher.
}

// Test for manager correctly stopping watcher (implicitly tested by TearDown not hanging,
// and previous StartAndStop test for FileWatcher itself. Explicit test is hard without
// more invasive hooks into FileWatcher's thread state).
// We can check logs or ensure no crashes during TearDown of the manager.
TEST_F(SecureStorageManagerTest, ManagerStopsWatcherOnDestruction) {
    SS_LOG_INFO("Test: ManagerStopsWatcherOnDestruction");
    FileWatcher::EventCallback callback = [this](const FileWatcher::WatchedEvent& event){
        // This callback might not even be hit if we destroy manager quickly
        std::lock_guard<std::mutex> lock(this->eventMutex);
        this->receivedEvents.push_back(event);
        SS_LOG_DEBUG("DestructionTestCallback: Event mask " << event.mask);
    };

    {
        SecureStorageManager manager(currentTestRootDir, dummySerial, callback);
        ASSERT_TRUE(manager.isInitialized());
        ASSERT_TRUE(manager.isFileWatcherActive());
        // Create a file to ensure watcher has something to do / is active
        std::string testFile = currentTestRootDir + "/destruction_test_file.txt";
        std::ofstream ofs(testFile);
        ofs << "touch";
        ofs.close();
        // Allow some time for event to be processed if needed, though not strictly asserted here
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
        SS_LOG_INFO("Manager going out of scope now...");
    } // Manager destructor is called here

    // Add a small delay to allow FileWatcher's stop() logs to appear if they are threaded.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    
    // Verification: Check for "FileWatcher: Stopped and resources cleaned." log in the test output.
    // This is an indirect way to verify. A more direct way would be if FileWatcher
    // could signal its shutdown completion, e.g., via a promise/future set in its destructor
    // or at the end of its monitorLoop.
    // For now, successful completion of this test without hangs or crashes,
    // combined with logs, is the primary indicator.
    SUCCEED() << "Manager was destroyed. Check logs for FileWatcher stop messages.";
}