// SecureStorage/tests/manager/TestSecureStorageManager.cpp
#include "gtest/gtest.h"
#include "SecureStorageManager.h" // Main public API header
#include "Error.h"   // For Errc
#include "FileUtil.h"  // For cleaning up test directories, pathExists
#include "Logger.h"  // For SS_LOG macros

#include <vector>
#include <string>
#include <thread>   // For std::this_thread::get_id
#include <chrono>   // For std::chrono
#include <fstream>  // For creating dummy files if needed for setup

// POSIX includes for directory manipulation if FileUtil's helpers aren't enough for test cleanup
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h> // For rmdir/unlink if needed for manual test cleanup

using namespace SecureStorage; // Access SecureStorageManager, Error::Errc
// Explicitly qualify sub-namespaces if needed, e.g., SecureStorage::Utils::FileUtil

class SecureStorageManagerTest : public ::testing::Test {
protected:
    std::string testBaseDir;
    std::string currentTestRootDir; // Root storage path for SecureStorageManager
    std::string dummySerial = "MgrTestSerial789";

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
};

TEST_F(SecureStorageManagerTest, InitializationSuccess) {
    SecureStorageManager manager(currentTestRootDir, dummySerial);
    EXPECT_TRUE(manager.isInitialized());
}

TEST_F(SecureStorageManagerTest, InitializationFailsWithEmptyRootPath) {
    SecureStorageManager manager("", dummySerial);
    EXPECT_FALSE(manager.isInitialized());
}

TEST_F(SecureStorageManagerTest, InitializationFailsWithEmptySerial) {
    SecureStorageManager manager(currentTestRootDir, "");
    EXPECT_FALSE(manager.isInitialized());
}

TEST_F(SecureStorageManagerTest, OperationsFailIfNotInitialized) {
    SecureStorageManager manager("", ""); // Force initialization failure
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
    SecureStorageManager manager(currentTestRootDir, dummySerial);
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
    SecureStorageManager manager(currentTestRootDir, dummySerial);
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
    SecureStorageManager manager(currentTestRootDir, dummySerial);
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
    SecureStorageManager manager1(currentTestRootDir, dummySerial);
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
    SecureStorageManager manager1(currentTestRootDir, dummySerial);
    ASSERT_TRUE(manager1.isInitialized());
    std::string id1 = "move_assign_data1";
    std::vector<unsigned char> data1 = {'a', 's', 's', 'i', 'g', 'n', '1'};
    ASSERT_EQ(manager1.storeData(id1, data1), Error::Errc::Success);

    // Create a different root path for manager2 to ensure resources are distinct before move
    std::string anotherRootDir = testBaseDir + "/manager_test_root_assign";
    recursiveDelete(anotherRootDir);
    ASSERT_EQ(Utils::FileUtil::createDirectories(anotherRootDir), Error::Errc::Success);
    SecureStorageManager manager2(anotherRootDir, "SerialForManager2");
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