#include "gtest/gtest.h"
#include "KeyProvider.h"
#include "FileUtil.h"
#include "Logger.h"
#include <vector>
#include <string>
#include <iomanip>
#include <fstream>
#include <cstdio> // For std::remove

// Helper to convert byte vector to hex string for easy comparison
std::string bytesToHex(const std::vector<unsigned char>& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

// Test fixture for KeyProvider tests
class KeyProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_testDir = "key_provider_test_dir";
        SecureStorage::Utils::FileUtil::createDirectories(m_testDir);

        m_secretFilePath = m_testDir + "/test_secret.txt";
        std::ofstream secretFile(m_secretFilePath);
        ASSERT_TRUE(secretFile.is_open());
        secretFile << "this-is-a-super-secret-string";
        secretFile.close();
    }

    void TearDown() override {
        SecureStorage::Utils::FileUtil::deleteFile(m_secretFilePath);
        std::remove(m_testDir.c_str());
    }

    std::string m_testDir;
    std::string m_secretFilePath;
    const std::string m_serial = "123456789";
};

TEST_F(KeyProviderTest, DeriveKeySuccess) {
    SecureStorage::Crypto::KeyProvider kp(m_serial, m_secretFilePath);
    std::vector<unsigned char> key;
    size_t keyLen = 32; // AES-256

    SecureStorage::Error::Errc result = kp.getEncryptionKey(key, keyLen);
    ASSERT_EQ(result, SecureStorage::Error::Errc::Success);
    ASSERT_EQ(key.size(), keyLen);
    SS_LOG_INFO("Derived key (hex): " << bytesToHex(key));
}

TEST_F(KeyProviderTest, DeriveKeyDifferentLength) {
    std::string serial = "TestSerial001";
    SecureStorage::Crypto::KeyProvider kp(serial, m_secretFilePath);
    std::vector<unsigned char> key;
    size_t keyLen = 16; // AES-128

    SecureStorage::Error::Errc result = kp.getEncryptionKey(key, keyLen);
    ASSERT_EQ(result, SecureStorage::Error::Errc::Success);
    ASSERT_EQ(key.size(), keyLen);
    SS_LOG_INFO("Derived 16-byte key (hex): " << bytesToHex(key));
}

TEST_F(KeyProviderTest, EmptySerial) {
    std::string serial = ""; // Empty serial
    SecureStorage::Crypto::KeyProvider kp(serial, m_secretFilePath);
    std::vector<unsigned char> key;
    SecureStorage::Error::Errc result = kp.getEncryptionKey(key, 32);
    ASSERT_EQ(result, SecureStorage::Error::Errc::InvalidArgument);
    ASSERT_TRUE(key.empty());
}

TEST_F(KeyProviderTest, EmptySecretPath) {
    SecureStorage::Crypto::KeyProvider kp(m_serial, ""); // Empty path
    std::vector<unsigned char> key;
    SecureStorage::Error::Errc result = kp.getEncryptionKey(key, 32);
    ASSERT_EQ(result, SecureStorage::Error::Errc::InvalidArgument);
    ASSERT_TRUE(key.empty());
}

TEST_F(KeyProviderTest, NonExistentSecretFile) {
    std::string badPath = m_testDir + "/nonexistent.txt";
    SecureStorage::Crypto::KeyProvider kp(m_serial, badPath);
    std::vector<unsigned char> key;
    SecureStorage::Error::Errc result = kp.getEncryptionKey(key, 32);
    ASSERT_EQ(result, SecureStorage::Error::Errc::FileOpenFailed);
    ASSERT_TRUE(key.empty());
}

TEST_F(KeyProviderTest, EmptySecretFile) {
    std::string emptyFilePath = m_testDir + "/empty_secret.txt";
    std::ofstream emptyFile(emptyFilePath);
    emptyFile.close();

    SecureStorage::Crypto::KeyProvider kp(m_serial, emptyFilePath);
    std::vector<unsigned char> key;
    SecureStorage::Error::Errc result = kp.getEncryptionKey(key, 32);
    ASSERT_EQ(result, SecureStorage::Error::Errc::InvalidFile);
    ASSERT_TRUE(key.empty());

    SecureStorage::Utils::FileUtil::deleteFile(emptyFilePath);
}


TEST_F(KeyProviderTest, ZeroKeyLength) {
    std::string serial = "ValidSerial123";
    SecureStorage::Crypto::KeyProvider kp(serial, m_secretFilePath);
    std::vector<unsigned char> key;
    SecureStorage::Error::Errc result = kp.getEncryptionKey(key, 0);
    ASSERT_EQ(result, SecureStorage::Error::Errc::InvalidArgument);
    ASSERT_TRUE(key.empty());
}

TEST_F(KeyProviderTest, CustomSaltAndInfo) {
    std::string serial = "CustomParamsDevice";
    std::string salt = "MyUniqueAppSalt-SecureStorage";
    std::string info = "AES-Key-For-Specific-Feature";
    SecureStorage::Crypto::KeyProvider kp(serial, m_secretFilePath, salt, info);

    std::vector<unsigned char> key1;
    ASSERT_EQ(kp.getEncryptionKey(key1, 32), SecureStorage::Error::Errc::Success);
    ASSERT_EQ(key1.size(), 32);

    // Key derived with default salt/info should be different
    SecureStorage::Crypto::KeyProvider kp_default_salt_info(serial, m_secretFilePath);
    std::vector<unsigned char> key2;
    ASSERT_EQ(kp_default_salt_info.getEncryptionKey(key2, 32), SecureStorage::Error::Errc::Success);
    ASSERT_EQ(key2.size(), 32);

    ASSERT_NE(key1, key2) << "Keys should differ with different salt/info parameters.";
    SS_LOG_INFO("Key1 (custom salt/info) hex: " << bytesToHex(key1));
    SS_LOG_INFO("Key2 (default salt/info) hex: " << bytesToHex(key2));
}

TEST_F(KeyProviderTest, MoveSemantics) {
    std::string serial = "MoveSerial123";
    SecureStorage::Crypto::KeyProvider kp1(serial, m_secretFilePath);
    std::vector<unsigned char> key1;
    ASSERT_EQ(kp1.getEncryptionKey(key1, 32), SecureStorage::Error::Errc::Success);

    SecureStorage::Crypto::KeyProvider kp2(std::move(kp1)); // Move construction
    std::vector<unsigned char> key2;
    ASSERT_EQ(kp2.getEncryptionKey(key2, 32), SecureStorage::Error::Errc::Success);
    ASSERT_EQ(key1, key2);

    SecureStorage::Crypto::KeyProvider kp3("AnotherSerial", m_secretFilePath);
    kp3 = std::move(kp2); // Move assignment
    std::vector<unsigned char> key3;
    ASSERT_EQ(kp3.getEncryptionKey(key3, 32), SecureStorage::Error::Errc::Success);
    ASSERT_EQ(key1, key3);
}