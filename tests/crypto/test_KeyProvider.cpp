#include "gtest/gtest.h"
#include "KeyProvider.h" // Adjust path if needed based on include setup
#include "Logger.h"
#include <vector>
#include <string>
#include <iomanip> // For std::hex

// Helper to convert byte vector to hex string for easy comparison
std::string bytesToHex(const std::vector<unsigned char>& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

TEST(KeyProviderTest, DeriveKeySuccess) {
    std::string serial = "123456789";
    SecureStorage::Crypto::KeyProvider kp(serial);
    std::vector<unsigned char> key;
    size_t keyLen = 32; // AES-256

    SecureStorage::Error::Errc result = kp.getEncryptionKey(key, keyLen);
    ASSERT_EQ(result, SecureStorage::Error::Errc::Success);
    ASSERT_EQ(key.size(), keyLen);
    SS_LOG_INFO("Derived key (hex): " << bytesToHex(key));

    // For a truly verifiable test, you'd compare 'key' against a known good value
    // derived using the same inputs (serial, default salt, default info) with an
    // external HKDF tool (e.g., OpenSSL command line or a Python script).
    // Example (replace with actual pre-calculated value):
    // std::string expectedKeyHex = ".... a 64-char hex string ....";
    // ASSERT_EQ(bytesToHex(key), expectedKeyHex);
}

TEST(KeyProviderTest, DeriveKeyDifferentLength) {
    std::string serial = "TestSerial001";
    SecureStorage::Crypto::KeyProvider kp(serial);
    std::vector<unsigned char> key;
    size_t keyLen = 16; // AES-128

    SecureStorage::Error::Errc result = kp.getEncryptionKey(key, keyLen);
    ASSERT_EQ(result, SecureStorage::Error::Errc::Success);
    ASSERT_EQ(key.size(), keyLen);
    SS_LOG_INFO("Derived 16-byte key (hex): " << bytesToHex(key));
}

TEST(KeyProviderTest, EmptySerial) {
    std::string serial = ""; // Empty serial
    SecureStorage::Crypto::KeyProvider kp(serial);
    std::vector<unsigned char> key;
    SecureStorage::Error::Errc result = kp.getEncryptionKey(key, 32);
    ASSERT_EQ(result, SecureStorage::Error::Errc::InvalidArgument);
    ASSERT_TRUE(key.empty());
}

TEST(KeyProviderTest, ZeroKeyLength) {
    std::string serial = "ValidSerial123";
    SecureStorage::Crypto::KeyProvider kp(serial);
    std::vector<unsigned char> key;
    SecureStorage::Error::Errc result = kp.getEncryptionKey(key, 0);
    ASSERT_EQ(result, SecureStorage::Error::Errc::InvalidArgument);
    ASSERT_TRUE(key.empty());
}

TEST(KeyProviderTest, CustomSaltAndInfo) {
    std::string serial = "CustomParamsDevice";
    std::string salt = "MyUniqueAppSalt-SecureStorage";
    std::string info = "AES-Key-For-Specific-Feature";
    SecureStorage::Crypto::KeyProvider kp(serial, salt, info);
    
    std::vector<unsigned char> key1;
    ASSERT_EQ(kp.getEncryptionKey(key1, 32), SecureStorage::Error::Errc::Success);
    ASSERT_EQ(key1.size(), 32);

    // Key derived with default salt/info should be different
    SecureStorage::Crypto::KeyProvider kp_default_salt_info(serial); // Uses default salt/info
    std::vector<unsigned char> key2;
    ASSERT_EQ(kp_default_salt_info.getEncryptionKey(key2, 32), SecureStorage::Error::Errc::Success);
    ASSERT_EQ(key2.size(), 32);

    ASSERT_NE(key1, key2) << "Keys should differ with different salt/info parameters.";
    SS_LOG_INFO("Key1 (custom salt/info) hex: " << bytesToHex(key1));
    SS_LOG_INFO("Key2 (default salt/info) hex: " << bytesToHex(key2));
}

TEST(KeyProviderTest, MoveSemantics) {
    std::string serial = "MoveSerial123";
    SecureStorage::Crypto::KeyProvider kp1(serial);
    std::vector<unsigned char> key1;
    ASSERT_EQ(kp1.getEncryptionKey(key1, 32), SecureStorage::Error::Errc::Success);

    SecureStorage::Crypto::KeyProvider kp2(std::move(kp1)); // Move construction
    std::vector<unsigned char> key2;
    ASSERT_EQ(kp2.getEncryptionKey(key2, 32), SecureStorage::Error::Errc::Success);
    ASSERT_EQ(key1, key2); // Should produce the same key

    // Check that kp1 might be in a valid but unspecified (moved-from) state
    // For KeyProvider, as long as serial is moved, it should probably fail or behave consistently.
    // Given current impl, getEncryptionKey would fail if m_deviceSerialNumber is empty.
    // This part of test depends on how move leaves the source object.
    // If serial is empty, it should return InvalidArgument.
    // std::vector<unsigned char> key1_after_move;
    // ASSERT_NE(kp1.getEncryptionKey(key1_after_move, 32), SecureStorage::Error::Errc::Success);


    SecureStorage::Crypto::KeyProvider kp3("AnotherSerial");
    kp3 = std::move(kp2); // Move assignment
    std::vector<unsigned char> key3;
    ASSERT_EQ(kp3.getEncryptionKey(key3, 32), SecureStorage::Error::Errc::Success);
    ASSERT_EQ(key1, key3); // Key should still be derived from "MoveSerial123"
}