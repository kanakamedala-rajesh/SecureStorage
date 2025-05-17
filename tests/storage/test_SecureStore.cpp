#include "gtest/gtest.h"

#include "SecureStore.h" // Adjust path as per your include structure
#include "FileUtil.h"    // For direct file manipulation in tests
#include "Error.h"
#include "Logger.h"      // For SS_LOG_ macros if needed in test logic

#include <vector>
#include <string>
#include <fstream>
#include <algorithm> // For std::sort, std::find
#include <thread>    // For std::this_thread::get_id for unique dir names
#include <chrono>    // For unique dir names

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h> // For rmdir (though std::remove is used for files)

// For SecureStorage::Crypto components if not fully pulled by SecureStore.h for tests
// #include "Encryptor.h" // Included via SecureStore.h
// #include "KeyProvider.h" // Included via SecureStore.h

using namespace SecureStorage::Storage;
using namespace SecureStorage::Utils;
using namespace SecureStorage::Error;

// Test Fixture for SecureStore tests
class SecureStoreTest : public ::testing::Test {
protected:
    std::string testBaseDir;    // Base directory for all SecureStore tests
    std::string currentTestRootDir; // Unique root storage path for each test
    std::string dummySerial = "TestSerial12345";

    // Helper to recursively delete a directory (copied from FileUtilTest for self-containment or use a common test helper)
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
            testBaseDir = std::string(CMAKE_BINARY_DIR) + "/SecureStoreTests_temp";
        #else
            char* temp_env = std::getenv("TMPDIR");
            if (temp_env) testBaseDir = std::string(temp_env);
            else temp_env = std::getenv("TEMP");
            if (temp_env) testBaseDir = std::string(temp_env);
            else testBaseDir = ".";
            testBaseDir += "/SecureStoreTests_temp";
        #endif

        FileUtil::createDirectories(testBaseDir); // Ensure base test dir exists

        // Create a unique directory for this specific test to avoid interference
        std::ostringstream oss;
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        oss << testBaseDir << "/store_" << std::this_thread::get_id() << "_" << now_ms;
        currentTestRootDir = oss.str();

        recursiveDelete(currentTestRootDir); // Clean up from previous run if any
        ASSERT_EQ(FileUtil::createDirectories(currentTestRootDir), Errc::Success)
            << "Failed to create temporary root storage for test: " << currentTestRootDir;
        SS_LOG_INFO("SecureStoreTest: Using temporary root directory: " << currentTestRootDir);
    }

    void TearDown() override {
        SS_LOG_INFO("SecureStoreTest: Cleaning up temporary root directory: " << currentTestRootDir);
        recursiveDelete(currentTestRootDir);
    }

    // Helper to get full path to a data file within the current test root
    std::string getDataFilePath(const std::string& data_id) const {
        return currentTestRootDir + "/" + data_id + DATA_FILE_EXTENSION;
    }
    std::string getBackupFilePath(const std::string& data_id) const {
        return currentTestRootDir + "/" + data_id + DATA_FILE_EXTENSION + BACKUP_FILE_EXTENSION;
    }
};

TEST_F(SecureStoreTest, InitializationSuccess) {
    SecureStore store(currentTestRootDir, dummySerial);
    ASSERT_TRUE(store.isInitialized());
}

TEST_F(SecureStoreTest, InitializationFailsWithEmptySerial) {
    SecureStore store(currentTestRootDir, ""); // Empty serial
    ASSERT_FALSE(store.isInitialized());
}

TEST_F(SecureStoreTest, InitializationFailsWithEmptyRootPath) {
    // Note: SecureStore constructor currently prevents this before FileUtil call
    // This test verifies that behavior.
    SecureStore store("", dummySerial); // Empty root path
    ASSERT_FALSE(store.isInitialized());
}

// This test is harder to make reliable as createDirectories might succeed in some OS-specific invalid paths
// or permissions might prevent actual failure in a controlled way for this test.
// TEST_F(SecureStoreTest, InitializationFailsIfRootPathUncreatable) {
//     std::string uncreatablePath = "/this_path_should_not_be_creatable_by_test/hopefully";
//     SecureStore store(uncreatablePath, dummySerial);
//     ASSERT_FALSE(store.isInitialized());
// }

TEST_F(SecureStoreTest, StoreAndRetrieveData) {
    SecureStore store(currentTestRootDir, dummySerial);
    ASSERT_TRUE(store.isInitialized());

    std::string id = "test_data_1";
    std::vector<unsigned char> data_to_store = {'h', 'e', 'l', 'l', 'o'};
    std::vector<unsigned char> retrieved_data;

    ASSERT_EQ(store.storeData(id, data_to_store), Errc::Success);
    ASSERT_TRUE(store.dataExists(id));
    ASSERT_TRUE(FileUtil::pathExists(getDataFilePath(id))); // Check main file exists

    ASSERT_EQ(store.retrieveData(id, retrieved_data), Errc::Success);
    ASSERT_EQ(retrieved_data, data_to_store);
}

TEST_F(SecureStoreTest, StoreEmptyData) {
    SecureStore store(currentTestRootDir, dummySerial);
    ASSERT_TRUE(store.isInitialized());
    std::string id = "empty_data";
    std::vector<unsigned char> empty_vec;
    std::vector<unsigned char> retrieved_data;

    ASSERT_EQ(store.storeData(id, empty_vec), Errc::Success);
    ASSERT_TRUE(store.dataExists(id));
    ASSERT_EQ(store.retrieveData(id, retrieved_data), Errc::Success);
    ASSERT_TRUE(retrieved_data.empty());
}

TEST_F(SecureStoreTest, RetrieveNonExistentData) {
    SecureStore store(currentTestRootDir, dummySerial);
    ASSERT_TRUE(store.isInitialized());
    std::string id = "non_existent";
    std::vector<unsigned char> retrieved_data;

    ASSERT_FALSE(store.dataExists(id));
    ASSERT_EQ(store.retrieveData(id, retrieved_data), Errc::DataNotFound);
    ASSERT_TRUE(retrieved_data.empty());
}

TEST_F(SecureStoreTest, DeleteData) {
    SecureStore store(currentTestRootDir, dummySerial);
    ASSERT_TRUE(store.isInitialized());
    std::string id = "to_delete";
    std::vector<unsigned char> data_to_store = {'d', 'e', 'l'};

    ASSERT_EQ(store.storeData(id, data_to_store), Errc::Success);
    ASSERT_TRUE(store.dataExists(id));

    ASSERT_EQ(store.deleteData(id), Errc::Success);
    ASSERT_FALSE(store.dataExists(id));
    ASSERT_FALSE(FileUtil::pathExists(getDataFilePath(id)));
    ASSERT_FALSE(FileUtil::pathExists(getBackupFilePath(id)));

    std::vector<unsigned char> retrieved_data;
    ASSERT_EQ(store.retrieveData(id, retrieved_data), Errc::DataNotFound);
}

TEST_F(SecureStoreTest, DeleteNonExistentData) {
    SecureStore store(currentTestRootDir, dummySerial);
    ASSERT_TRUE(store.isInitialized());
    std::string id = "already_gone";
    ASSERT_FALSE(store.dataExists(id));
    ASSERT_EQ(store.deleteData(id), Errc::Success); // Should succeed
}

TEST_F(SecureStoreTest, OverwriteData) {
    SecureStore store(currentTestRootDir, dummySerial);
    ASSERT_TRUE(store.isInitialized());
    std::string id = "overwrite_me";
    std::vector<unsigned char> data1 = {'o', 'l', 'd'};
    std::vector<unsigned char> data2 = {'n', 'e', 'w'};
    std::vector<unsigned char> retrieved_data;

    ASSERT_EQ(store.storeData(id, data1), Errc::Success);
    ASSERT_EQ(store.retrieveData(id, retrieved_data), Errc::Success);
    ASSERT_EQ(retrieved_data, data1);

    // Check backup file exists after first store (if main was created, then moved to backup during second store's process)
    // This depends on the exact backup logic: storeData's logic should make old main the backup
    // For this to be testable, the second store must happen.
    std::string mainFile1 = getDataFilePath(id);
    std::string backupFile1 = getBackupFilePath(id);


    ASSERT_EQ(store.storeData(id, data2), Errc::Success); // Overwrite with new data
    ASSERT_TRUE(FileUtil::pathExists(mainFile1)); // New main file
    ASSERT_TRUE(FileUtil::pathExists(backupFile1)); // Old data should now be in backup

    ASSERT_EQ(store.retrieveData(id, retrieved_data), Errc::Success);
    ASSERT_EQ(retrieved_data, data2); // Should get the new data
}

TEST_F(SecureStoreTest, InvalidDataId) {
    SecureStore store(currentTestRootDir, dummySerial);
    ASSERT_TRUE(store.isInitialized());
    std::vector<unsigned char> data = {'a'};
    std::vector<unsigned char> retrieved_data;

    ASSERT_EQ(store.storeData("", data), Errc::InvalidArgument);
    ASSERT_EQ(store.storeData("bad/id", data), Errc::InvalidArgument);
    ASSERT_EQ(store.storeData("bad\\id", data), Errc::InvalidArgument);
    ASSERT_EQ(store.storeData("bad..id", data), Errc::InvalidArgument);

    ASSERT_EQ(store.retrieveData("bad/id", retrieved_data), Errc::InvalidArgument);
    ASSERT_FALSE(store.dataExists("bad/id"));
    ASSERT_EQ(store.deleteData("bad/id"), Errc::InvalidArgument);
}

TEST_F(SecureStoreTest, ListDataIds) {
    SecureStore store(currentTestRootDir, dummySerial);
    ASSERT_TRUE(store.isInitialized());
    std::vector<std::string> ids;

    ASSERT_EQ(store.listDataIds(ids), Errc::Success);
    ASSERT_TRUE(ids.empty());

    ASSERT_EQ(store.storeData("id1", {'1'}), Errc::Success);
    ASSERT_EQ(store.storeData("id2", {'2'}), Errc::Success);
    // Create a decoy backup file to ensure it's not listed
    std::ofstream(getBackupFilePath("id_bak_only")).put('b');
    // Create a decoy temp file
    std::ofstream(currentTestRootDir + "/id_tmp_only" + DATA_FILE_EXTENSION + TEMP_FILE_SUFFIX).put('t');


    ASSERT_EQ(store.listDataIds(ids), Errc::Success);
    ASSERT_EQ(ids.size(), 2);
    // listDataIds sorts them
    ASSERT_NE(std::find(ids.begin(), ids.end(), "id1"), ids.end());
    ASSERT_NE(std::find(ids.begin(), ids.end(), "id2"), ids.end());

    ASSERT_EQ(store.deleteData("id1"), Errc::Success);
    ASSERT_EQ(store.listDataIds(ids), Errc::Success);
    ASSERT_EQ(ids.size(), 1);
    ASSERT_EQ(ids[0], "id2");
}

TEST_F(SecureStoreTest, RetrieveFromBackupAndRestore) {
    SecureStore store(currentTestRootDir, dummySerial);
    ASSERT_TRUE(store.isInitialized());

    std::string id = "backup_test";
    std::vector<unsigned char> data = {'b', 'a', 'c', 'k', 'u', 'p'};
    ASSERT_EQ(store.storeData(id, data), Errc::Success); // Stores to main

    std::string mainFile = getDataFilePath(id);
    std::string backupFile = getBackupFilePath(id);

    // To ensure a distinct backup, we store again, making current main the backup
    std::vector<unsigned char> data_v2 = {'v', '2'};
    ASSERT_EQ(store.storeData(id, data_v2), Errc::Success);
    // Now, 'backupFile' should contain original 'data', 'mainFile' should contain 'data_v2'
    
    // Verify this setup by reading backup content directly (decrypting it separately)
    // This is a bit of a white-box test to confirm backup content.
    std::vector<unsigned char> backup_encrypted_content;
    ASSERT_EQ(FileUtil::readFile(backupFile, backup_encrypted_content), Errc::Success);
    ASSERT_FALSE(backup_encrypted_content.empty());
    std::vector<unsigned char> backup_decrypted_content;
    // Need an encryptor instance to decrypt manually for test verification
    SecureStorage::Crypto::Encryptor temp_encryptor;
    std::unique_ptr<SecureStorage::Crypto::KeyProvider> temp_kp(new SecureStorage::Crypto::KeyProvider(dummySerial)); // Fully qualify namespace
    std::vector<unsigned char> master_key_for_test;
    ASSERT_EQ(temp_kp->getEncryptionKey(master_key_for_test, SecureStorage::Crypto::AES_GCM_KEY_SIZE_BYTES), Errc::Success); // Fully qualify namespace
    ASSERT_EQ(temp_encryptor.decrypt(backup_encrypted_content, master_key_for_test, backup_decrypted_content), Errc::Success);
    ASSERT_EQ(backup_decrypted_content, data); // Backup has original data

    // Now corrupt the main file (which has data_v2)
    std::ofstream ofs(mainFile, std::ios::binary | std::ios::trunc);
    std::string junk_data = "This is definitely junk data for corruption and is long enough 123456768."; // > 28 bytes
    ofs.write(junk_data.data(), junk_data.size());
    ofs.close();

    std::vector<unsigned char> retrieved_data;
    // This retrieveData should fail on main, then read backup (original 'data'), decrypt it, and restore it to main.
    ASSERT_EQ(store.retrieveData(id, retrieved_data), Errc::Success);
    ASSERT_EQ(retrieved_data, data); // Should be original 'data' from backup

    // Verify main file was restored with backup's content
    std::vector<unsigned char> main_file_content_after_restore_encrypted;
    ASSERT_EQ(FileUtil::readFile(mainFile, main_file_content_after_restore_encrypted), Errc::Success);
    std::vector<unsigned char> main_file_content_after_restore_decrypted;
    ASSERT_EQ(temp_encryptor.decrypt(main_file_content_after_restore_encrypted, master_key_for_test, main_file_content_after_restore_decrypted), Errc::Success);
    ASSERT_EQ(main_file_content_after_restore_decrypted, data);
}

TEST_F(SecureStoreTest, RetrieveFailsIfMainCorruptAndBackupMissing) {
    SecureStore store(currentTestRootDir, dummySerial);
    ASSERT_TRUE(store.isInitialized());
    std::string id = "main_corrupt_no_backup";
    std::vector<unsigned char> data = {'t', 'e', 's', 't'};

    ASSERT_EQ(store.storeData(id, data), Errc::Success); // Creates mainFile

    std::string mainFile = getDataFilePath(id);
    std::string backupFile = getBackupFilePath(id);
    ASSERT_EQ(FileUtil::deleteFile(backupFile), Errc::Success); // Ensure no backup

    // Corrupt main file
    std::ofstream ofs(mainFile, std::ios::binary | std::ios::trunc);
    ofs << "junk data that won't decrypt";
    ofs.close();

    std::vector<unsigned char> retrieved_data;
    Errc result = store.retrieveData(id, retrieved_data);
    // Expected: DecryptionFailed or AuthenticationFailed for the main file,
    // then DataNotFound because backup also fails.
    // The exact error depends on how deep the retry goes.
    // retrieveData should return the error from the last failed attempt.
    ASSERT_TRUE(result == Errc::AuthenticationFailed || result == Errc::DecryptionFailed || result == Errc::DataNotFound);
    ASSERT_TRUE(retrieved_data.empty());
}

TEST_F(SecureStoreTest, RetrieveFailsIfMainMissingAndBackupCorrupt) {
    SecureStore store(currentTestRootDir, dummySerial);
    ASSERT_TRUE(store.isInitialized());
    std::string id = "main_missing_backup_corrupt";
    std::vector<unsigned char> data = {'d', 'a', 't', 'a'};

    // Store data, then store again to ensure original data is in backup
    ASSERT_EQ(store.storeData(id, data), Errc::Success);
    ASSERT_EQ(store.storeData(id, {'v','2'}), Errc::Success); // data is now in backup

    std::string mainFile = getDataFilePath(id);
    std::string backupFile = getBackupFilePath(id);

    ASSERT_EQ(FileUtil::deleteFile(mainFile), Errc::Success); // Main file gone

    // Corrupt backup file
    std::ofstream ofs(backupFile, std::ios::binary | std::ios::trunc);
    std::string junk_data = "This is also junk backup data and it's sufficiently long for test."; // > 28 bytes
    ofs.write(junk_data.data(), junk_data.size());
    ofs.close();

    std::vector<unsigned char> retrieved_data;
    Errc result = store.retrieveData(id, retrieved_data);
    ASSERT_TRUE(result == Errc::AuthenticationFailed || result == Errc::DecryptionFailed)
        << SecureStorageErrorCategory::get().message(static_cast<int>(result));
    ASSERT_TRUE(retrieved_data.empty());
}