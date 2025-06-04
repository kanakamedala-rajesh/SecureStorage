#include "gtest/gtest.h"
#include "crypto/KeyProvider.h" // Adjusted path to be more specific
#include "utils/ISystemIdProvider.h" // For the interface and mock
#include "utils/Logger.h"      // Adjusted path
#include "utils/Error.h"       // For Errc codes
#include <vector>
#include <string>
#include <iomanip> // For std::hex

// Mock System ID Provider
class MockSystemIdProvider : public SecureStorage::Utils::ISystemIdProvider {
public:
    std::string systemIdToReturn;
    SecureStorage::Error::Errc errorToReturn;
    mutable int callCount = 0; // Mutable to allow modification in const method

    MockSystemIdProvider() : errorToReturn(SecureStorage::Error::Errc::Success) {}

    SecureStorage::Error::Errc getSystemId(std::string& systemId) const override {
        callCount++;
        if (errorToReturn != SecureStorage::Error::Errc::Success) {
            return errorToReturn;
        }
        systemId = systemIdToReturn;
        return SecureStorage::Error::Errc::Success;
    }
};

using namespace SecureStorage::Crypto;
using namespace SecureStorage::Utils;
using namespace SecureStorage::Error;


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
    MockSystemIdProvider mockIdProvider;
    // KeyProvider instance will be created in each test case
};


TEST_F(KeyProviderTest, DeriveKeySuccess) {
    mockIdProvider.systemIdToReturn = "test_system_id_12345";
    KeyProvider kp(mockIdProvider);
    std::vector<unsigned char> key;
    size_t keyLen = 32; // AES-256

    Errc result = kp.getEncryptionKey(key, keyLen);
    ASSERT_EQ(result, Errc::Success);
    ASSERT_EQ(key.size(), keyLen);
    ASSERT_FALSE(key.empty());
    ASSERT_EQ(mockIdProvider.callCount, 1);
    SS_LOG_INFO("Derived key (hex): " << bytesToHex(key));
}

TEST_F(KeyProviderTest, DeriveKeyDifferentLength) {
    mockIdProvider.systemIdToReturn = "TestSystemId001";
    KeyProvider kp(mockIdProvider);
    std::vector<unsigned char> key;
    size_t keyLen = 16; // AES-128

    Errc result = kp.getEncryptionKey(key, keyLen);
    ASSERT_EQ(result, Errc::Success);
    ASSERT_EQ(key.size(), keyLen);
    ASSERT_EQ(mockIdProvider.callCount, 1);
    SS_LOG_INFO("Derived 16-byte key (hex): " << bytesToHex(key));
}

TEST_F(KeyProviderTest, GetEncryptionKey_ProviderReturnsEmptySystemId) {
    mockIdProvider.systemIdToReturn = ""; // Empty system ID from provider
    KeyProvider kp(mockIdProvider);
    std::vector<unsigned char> key;
    Errc result = kp.getEncryptionKey(key, 32);

    ASSERT_EQ(result, Errc::InvalidArgument);
    ASSERT_TRUE(key.empty());
    ASSERT_EQ(mockIdProvider.callCount, 1);
}

TEST_F(KeyProviderTest, GetEncryptionKey_ProviderReturnsError) {
    mockIdProvider.errorToReturn = Errc::SystemError; // Mock provider simulates an error
    KeyProvider kp(mockIdProvider);
    std::vector<unsigned char> key;
    Errc result = kp.getEncryptionKey(key, 32);

    ASSERT_EQ(result, Errc::SystemError); // Error should propagate
    ASSERT_TRUE(key.empty());
    ASSERT_EQ(mockIdProvider.callCount, 1);
}


TEST_F(KeyProviderTest, ZeroKeyLength) {
    mockIdProvider.systemIdToReturn = "ValidSystemId123"; // System ID must be valid for this check
    KeyProvider kp(mockIdProvider);
    std::vector<unsigned char> key;
    Errc result = kp.getEncryptionKey(key, 0);
    ASSERT_EQ(result, Errc::InvalidArgument);
    ASSERT_TRUE(key.empty());
    // getSystemId should not be called if keyLength is 0
    ASSERT_EQ(mockIdProvider.callCount, 0);
}

TEST_F(KeyProviderTest, CustomSaltAndInfo) {
    mockIdProvider.systemIdToReturn = "CustomParamsSystem";
    std::string salt = "MyUniqueAppSalt-SecureStorage";
    std::string info = "AES-Key-For-Specific-Feature";
    KeyProvider kp(mockIdProvider, salt, info);
    
    std::vector<unsigned char> key1;
    ASSERT_EQ(kp.getEncryptionKey(key1, 32), Errc::Success);
    ASSERT_EQ(key1.size(), 32);
    ASSERT_EQ(mockIdProvider.callCount, 1);

    // Key derived with default salt/info should be different
    // Need a new KeyProvider instance or reset mock call count for distinct test
    mockIdProvider.callCount = 0; // Reset for the next call
    KeyProvider kp_default_salt_info(mockIdProvider); // Uses default salt/info
    std::vector<unsigned char> key2;
    ASSERT_EQ(kp_default_salt_info.getEncryptionKey(key2, 32), Errc::Success);
    ASSERT_EQ(key2.size(), 32);
    ASSERT_EQ(mockIdProvider.callCount, 1);


    ASSERT_NE(key1, key2) << "Keys should differ with different salt/info parameters.";
    SS_LOG_INFO("Key1 (custom salt/info) hex: " << bytesToHex(key1));
    SS_LOG_INFO("Key2 (default salt/info) hex: " << bytesToHex(key2));
}

TEST_F(KeyProviderTest, DifferentSystemIdsProduceDifferentKeys) {
    mockIdProvider.systemIdToReturn = "system_id_A";
    KeyProvider kp1(mockIdProvider);
    std::vector<unsigned char> key1;
    ASSERT_EQ(kp1.getEncryptionKey(key1, 32), Errc::Success);
    ASSERT_EQ(mockIdProvider.callCount, 1);

    mockIdProvider.systemIdToReturn = "system_id_B";
    mockIdProvider.callCount = 0; // Reset for the next call
    KeyProvider kp2(mockIdProvider); // Assumes kp2 uses the updated mockIdProvider state
    std::vector<unsigned char> key2;
    ASSERT_EQ(kp2.getEncryptionKey(key2, 32), Errc::Success);
    ASSERT_EQ(mockIdProvider.callCount, 1);

    ASSERT_NE(key1, key2) << "Keys derived from different system IDs should not be identical.";
}


TEST_F(KeyProviderTest, MoveSemantics) {
    mockIdProvider.systemIdToReturn = "MoveSystemId123";
    KeyProvider kp1(mockIdProvider);
    std::vector<unsigned char> key1;
    ASSERT_EQ(kp1.getEncryptionKey(key1, 32), Errc::Success);
    ASSERT_EQ(mockIdProvider.callCount, 1);
    std::string key1_hex = bytesToHex(key1); // Store for later comparison

    mockIdProvider.callCount = 0; // Reset before move
    KeyProvider kp2(std::move(kp1)); // Move construction
    std::vector<unsigned char> key2;
    // The mockIdProvider is captured by reference, so kp2 should still be able to use it.
    ASSERT_EQ(kp2.getEncryptionKey(key2, 32), Errc::Success);
    ASSERT_EQ(bytesToHex(key2), key1_hex);
    ASSERT_EQ(mockIdProvider.callCount, 1); // Mock was called again

    // Test behavior of moved-from object kp1
    // Accessing a moved-from KeyProvider's methods that depend on the PImpl (m_impl)
    // or the reference (m_systemIdProvider) is generally okay if the PImpl is just moved
    // and the reference remains valid. The PImpl unique_ptr in kp1 will be null.
    // KeyProvider's getEncryptionKey doesn't directly check m_impl for nullity
    // but relies on members like m_salt, m_info, and m_systemIdProvider.
    // The salt and info strings are moved. The reference m_systemIdProvider is copied.
    // The current implementation of getEncryptionKey would likely lead to a crash or
    // undefined behavior if it tried to use moved salt/info strings.
    // A robust moved-from state would have defined behavior, e.g., throw or return an error.
    // For now, we won't test kp1 after move, as its state is "valid but unspecified"
    // and using it may not be safe without more specific guarantees from KeyProvider's move ops.

    mockIdProvider.callCount = 0; // Reset before move assignment
    KeyProvider kp3(mockIdProvider); // A new provider to be overwritten
    kp3 = std::move(kp2); // Move assignment
    std::vector<unsigned char> key3;
    ASSERT_EQ(kp3.getEncryptionKey(key3, 32), Errc::Success);
    ASSERT_EQ(bytesToHex(key3), key1_hex);
    ASSERT_EQ(mockIdProvider.callCount, 1);
}