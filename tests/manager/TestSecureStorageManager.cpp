#include "gtest/gtest.h"
#include "SecureStorageManager.h"
#include "Error.h"
#include "FileUtil.h"
#include "Logger.h"
#include "file_watcher/FileWatcher.h"
#include "storage/SecureStore.h"

#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <algorithm>

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cstdio>

#ifndef IN_CREATE
#include <sys/inotify.h>
#endif

using namespace SecureStorage;

class SecureStorageManagerTest : public ::testing::Test {
protected:
    std::string testBaseDir;
    std::string currentTestRootDir;
    std::string dummySerial = "MgrTestSerial789";
    std::string m_secretFilePath;

    std::vector<FileWatcher::WatchedEvent> receivedEvents;
    mutable std::mutex eventMutex;
    std::condition_variable eventCv;
    size_t expectedEventCount = 0;

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
        std::remove(path.c_str());
    }

    void SetUp() override {
        testBaseDir = "./SecureStorageManagerTests_temp";
        Utils::FileUtil::createDirectories(testBaseDir);

        std::ostringstream oss;
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        oss << testBaseDir << "/manager_test_root_" << std::this_thread::get_id() << "_" << now_ms;
        currentTestRootDir = oss.str();
        recursiveDelete(currentTestRootDir);
        ASSERT_EQ(Utils::FileUtil::createDirectories(currentTestRootDir), Error::Errc::Success);
        SS_LOG_INFO("SecureStorageManagerTest: Using temp root: " << currentTestRootDir);

        m_secretFilePath = currentTestRootDir + "/secret.txt";
        std::ofstream secretFile(m_secretFilePath);
        ASSERT_TRUE(secretFile.is_open());
        secretFile << "manager-secret";
        secretFile.close();
    }

    void TearDown() override {
        SS_LOG_INFO("SecureStorageManagerTest: Cleaning up temp root: " << currentTestRootDir);
        recursiveDelete(testBaseDir);
    }

    FileWatcher::EventCallback getTestEventCallback() {
        return [this](const FileWatcher::WatchedEvent& event) {
            std::lock_guard<std::mutex> lock(this->eventMutex);
            this->receivedEvents.push_back(event);
            SS_LOG_DEBUG("SSM TestCallback: Received event for path='" << event.filePath << "', name='" << event.fileName << "'");
            if (this->receivedEvents.size() >= this->expectedEventCount) {
                this->eventCv.notify_all();
            }
        };
    }

    bool waitForEvents(size_t count, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
        expectedEventCount = count;
        std::unique_lock<std::mutex> lock(eventMutex);
        if (receivedEvents.size() >= count) return true;
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
    SecureStorageManager manager(currentTestRootDir, dummySerial, m_secretFilePath, getTestEventCallback());
    EXPECT_TRUE(manager.isInitialized());
}

TEST_F(SecureStorageManagerTest, InitializationFailsWithEmptyRootPath) {
    SecureStorageManager manager("", dummySerial, m_secretFilePath, getTestEventCallback());
    EXPECT_FALSE(manager.isInitialized());
}

TEST_F(SecureStorageManagerTest, InitializationFailsWithEmptySerial) {
    SecureStorageManager manager(currentTestRootDir, "", m_secretFilePath, getTestEventCallback());
    EXPECT_FALSE(manager.isInitialized());
}

TEST_F(SecureStorageManagerTest, InitializationFailsWithEmptySecretPath) {
    SecureStorageManager manager(currentTestRootDir, dummySerial, "", getTestEventCallback());
    EXPECT_FALSE(manager.isInitialized());
}

TEST_F(SecureStorageManagerTest, OperationsFailIfNotInitialized) {
    SecureStorageManager manager("", "", "", getTestEventCallback()); // Force initialization failure
    ASSERT_FALSE(manager.isInitialized());

    std::string id = "test_id";
    std::vector<unsigned char> data_to_store = {'a'};
    std::vector<unsigned char> retrieved_data;
    std::vector<std::string> ids_list;

    EXPECT_EQ(manager.storeData(id, data_to_store), Error::Errc::NotInitialized);
    EXPECT_EQ(manager.retrieveData(id, retrieved_data), Error::Errc::NotInitialized);
    EXPECT_EQ(manager.deleteData(id), Error::Errc::NotInitialized);
    EXPECT_FALSE(manager.dataExists(id));
    EXPECT_EQ(manager.listDataIds(ids_list), Error::Errc::NotInitialized);
}

TEST_F(SecureStorageManagerTest, BasicStoreAndRetrieveDelegation) {
    SecureStorageManager manager(currentTestRootDir, dummySerial, m_secretFilePath, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized());

    std::string id = "delegation_test";
    std::vector<unsigned char> store_vec = {'s', 't', 'o', 'r', 'e'};
    std::vector<unsigned char> retrieve_vec;

    ASSERT_EQ(manager.storeData(id, store_vec), Error::Errc::Success);
    ASSERT_TRUE(manager.dataExists(id));
    ASSERT_EQ(manager.retrieveData(id, retrieve_vec), Error::Errc::Success);
    ASSERT_EQ(retrieve_vec, store_vec);
}

TEST_F(SecureStorageManagerTest, MoveConstructor) {
    SecureStorageManager manager1(currentTestRootDir, dummySerial, m_secretFilePath, getTestEventCallback());
    ASSERT_TRUE(manager1.isInitialized());
    std::string id = "move_test_data";
    std::vector<unsigned char> data = {'m', 'o', 'v', 'e'};
    ASSERT_EQ(manager1.storeData(id, data), Error::Errc::Success);

    SecureStorageManager manager2(std::move(manager1));
    ASSERT_TRUE(manager2.isInitialized());
    ASSERT_FALSE(manager1.isInitialized());

    std::vector<unsigned char> retrieved_data;
    ASSERT_EQ(manager2.retrieveData(id, retrieved_data), Error::Errc::Success);
    ASSERT_EQ(retrieved_data, data);
    ASSERT_EQ(manager1.retrieveData(id, retrieved_data), Error::Errc::NotInitialized);
}

TEST_F(SecureStorageManagerTest, InitializationWithFileWatcher) {
    SecureStorageManager manager(currentTestRootDir, dummySerial, m_secretFilePath, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized());
    ASSERT_TRUE(manager.isFileWatcherActive());
}

TEST_F(SecureStorageManagerTest, WatcherDetectsExternalFileCreation) {
    SecureStorageManager manager(currentTestRootDir, dummySerial, m_secretFilePath, getTestEventCallback());
    ASSERT_TRUE(manager.isInitialized() && manager.isFileWatcherActive());

    std::string newFilePath = currentTestRootDir + "/externally_created.txt";
    std::ofstream ofs(newFilePath);
    ofs << "external content";
    ofs.close();

    ASSERT_TRUE(waitForEvents(2, std::chrono::seconds(2)));
    const FileWatcher::WatchedEvent* createEvent = findEvent(IN_CREATE, "externally_created.txt");
    ASSERT_NE(createEvent, nullptr);
}