#include "gtest/gtest.h"
#include "SecureStore.h"
#include "FileUtil.h"
#include "Error.h"
#include "Logger.h"
#include "Encryptor.h"
#include "KeyProvider.h"

#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstdio>

// For system calls like opendir, stat, remove
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace SecureStorage::Storage;
using namespace SecureStorage::Utils;
using namespace SecureStorage::Error;

// Test Fixture for SecureStore tests
class SecureStoreTest : public ::testing::Test {
protected:
    std::string testBaseDir;
    std::string currentTestRootDir;
    std::string m_secretFilePath;
    const std::string dummySerial = "TestSerial12345";

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
        testBaseDir = "./SecureStoreTests_temp";
        FileUtil::createDirectories(testBaseDir);

        std::ostringstream oss;
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        oss << testBaseDir << "/store_" << std::this_thread::get_id() << "_" << now_ms;
        currentTestRootDir = oss.str();
        recursiveDelete(currentTestRootDir);
        ASSERT_EQ(FileUtil::createDirectories(currentTestRootDir), Errc::Success);
        SS_LOG_INFO("SecureStoreTest: Using temporary root directory: " << currentTestRootDir);

        m_secretFilePath = currentTestRootDir + "/test_secret.txt";
        std::ofstream secretFile(m_secretFilePath);
        ASSERT_TRUE(secretFile.is_open());
        secretFile << "a-device-specific-secret-key";
        secretFile.close();
    }

    void TearDown() override {
        SS_LOG_INFO("SecureStoreTest: Cleaning up temporary root directory: " << currentTestRootDir);
        recursiveDelete(testBaseDir);
    }

    std::string getDataFilePath(const std::string& data_id) const {
        return currentTestRootDir + "/" + data_id + DATA_FILE_EXTENSION;
    }
    std::string getBackupFilePath(const std::string& data_id) const {
        return currentTestRootDir + "/" + data_id + DATA_FILE_EXTENSION + BACKUP_FILE_EXTENSION;
    }
};

TEST_F(SecureStoreTest, InitializationSuccess) {
    SecureStore store(currentTestRootDir, dummySerial, m_secretFilePath);
    ASSERT_TRUE(store.isInitialized());
}

TEST_F(SecureStoreTest, InitializationFailsWithEmptySerial) {
    SecureStore store(currentTestRootDir, "", m_secretFilePath);
    ASSERT_FALSE(store.isInitialized());
}

TEST_F(SecureStoreTest, InitializationFailsWithEmptyRootPath) {
    SecureStore store("", dummySerial, m_secretFilePath);
    ASSERT_FALSE(store.isInitialized());
}

TEST_F(SecureStoreTest, InitializationFailsWithEmptySecretPath) {
    SecureStore store(currentTestRootDir, dummySerial, "");
    ASSERT_FALSE(store.isInitialized());
}

TEST_F(SecureStoreTest, InitializationFailsWithNonExistentSecretFile) {
    SecureStore store(currentTestRootDir, dummySerial, "nonexistent/secret.txt");
    ASSERT_FALSE(store.isInitialized());
}

TEST_F(SecureStoreTest, StoreAndRetrieveData) {
    SecureStore store(currentTestRootDir, dummySerial, m_secretFilePath);
    ASSERT_TRUE(store.isInitialized());

    std::string id = "test_data_1";
    std::vector<unsigned char> data_to_store = {'h', 'e', 'l', 'l', 'o'};
    std::vector<unsigned char> retrieved_data;

    ASSERT_EQ(store.storeData(id, data_to_store), Errc::Success);
    ASSERT_TRUE(store.dataExists(id));
    ASSERT_TRUE(FileUtil::pathExists(getDataFilePath(id)));

    ASSERT_EQ(store.retrieveData(id, retrieved_data), Errc::Success);
    ASSERT_EQ(retrieved_data, data_to_store);
}

TEST_F(SecureStoreTest, StoreEmptyData) {
    SecureStore store(currentTestRootDir, dummySerial, m_secretFilePath);
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
    SecureStore store(currentTestRootDir, dummySerial, m_secretFilePath);
    ASSERT_TRUE(store.isInitialized());
    std::string id = "non_existent";
    std::vector<unsigned char> retrieved_data;

    ASSERT_FALSE(store.dataExists(id));
    ASSERT_EQ(store.retrieveData(id, retrieved_data), Errc::DataNotFound);
    ASSERT_TRUE(retrieved_data.empty());
}

TEST_F(SecureStoreTest, DeleteData) {
    SecureStore store(currentTestRootDir, dummySerial, m_secretFilePath);
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

TEST_F(SecureStoreTest, OverwriteData) {
    SecureStore store(currentTestRootDir, dummySerial, m_secretFilePath);
    ASSERT_TRUE(store.isInitialized());
    std::string id = "overwrite_me";
    std::vector<unsigned char> data1 = {'o', 'l', 'd'};
    std::vector<unsigned char> data2 = {'n', 'e', 'w'};
    std::vector<unsigned char> retrieved_data;

    ASSERT_EQ(store.storeData(id, data1), Errc::Success);
    ASSERT_EQ(store.retrieveData(id, retrieved_data), Errc::Success);
    ASSERT_EQ(retrieved_data, data1);

    std::string mainFile1 = getDataFilePath(id);
    std::string backupFile1 = getBackupFilePath(id);

    ASSERT_EQ(store.storeData(id, data2), Errc::Success);
    ASSERT_TRUE(FileUtil::pathExists(mainFile1));
    ASSERT_TRUE(FileUtil::pathExists(backupFile1));

    ASSERT_EQ(store.retrieveData(id, retrieved_data), Errc::Success);
    ASSERT_EQ(retrieved_data, data2);
}

TEST_F(SecureStoreTest, ListDataIds) {
    SecureStore store(currentTestRootDir, dummySerial, m_secretFilePath);
    ASSERT_TRUE(store.isInitialized());
    std::vector<std::string> ids;

    ASSERT_EQ(store.listDataIds(ids), Errc::Success);
    ASSERT_TRUE(ids.empty());

    ASSERT_EQ(store.storeData("id1", {'1'}), Errc::Success);
    ASSERT_EQ(store.storeData("id2", {'2'}), Errc::Success);

    ASSERT_EQ(store.listDataIds(ids), Errc::Success);
    ASSERT_EQ(ids.size(), 2);
    std::sort(ids.begin(), ids.end());
    ASSERT_EQ(ids[0], "id1");
    ASSERT_EQ(ids[1], "id2");
}

TEST_F(SecureStoreTest, RetrieveFromBackupAndRestore) {
    SecureStore store(currentTestRootDir, dummySerial, m_secretFilePath);
    ASSERT_TRUE(store.isInitialized());

    std::string id = "backup_test";
    std::vector<unsigned char> data = {'b', 'a', 'c', 'k', 'u', 'p'};
    ASSERT_EQ(store.storeData(id, data), Errc::Success);

    std::vector<unsigned char> data_v2 = {'v', '2'};
    ASSERT_EQ(store.storeData(id, data_v2), Errc::Success);
    
    std::string mainFile = getDataFilePath(id);
    std::string backupFile = getBackupFilePath(id);

    // Corrupt the main file
    std::ofstream ofs(mainFile, std::ios::binary | std::ios::trunc);
    ofs << "junk";
    ofs.close();

    std::vector<unsigned char> retrieved_data;
    ASSERT_EQ(store.retrieveData(id, retrieved_data), Errc::Success);
    ASSERT_EQ(retrieved_data, data);

    // Verify main file was restored
    std::vector<unsigned char> main_file_content_after_restore_encrypted;
    ASSERT_EQ(FileUtil::readFile(mainFile, main_file_content_after_restore_encrypted), Errc::Success);

    SecureStorage::Crypto::Encryptor temp_encryptor;
    std::unique_ptr<SecureStorage::Crypto::KeyProvider> temp_kp(
        new SecureStorage::Crypto::KeyProvider(dummySerial, m_secretFilePath)
    );
    std::vector<unsigned char> master_key_for_test;
    ASSERT_EQ(temp_kp->getEncryptionKey(master_key_for_test, 32), Errc::Success);

    std::vector<unsigned char> main_file_content_after_restore_decrypted;
    ASSERT_EQ(temp_encryptor.decrypt(main_file_content_after_restore_encrypted, master_key_for_test, main_file_content_after_restore_decrypted), Errc::Success);
    ASSERT_EQ(main_file_content_after_restore_decrypted, data);
}