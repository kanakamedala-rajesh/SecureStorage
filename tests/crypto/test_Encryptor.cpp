#include "gtest/gtest.h"
#include "Encryptor.h" // Adjust path
#include "Logger.h"
#include <vector>
#include <string>
#include <algorithm> // For std::equal

class EncryptorTest : public ::testing::Test {
protected:
    SecureStorage::Crypto::Encryptor encryptor;
    std::vector<unsigned char> key;
    std::vector<unsigned char> plaintext;
    std::vector<unsigned char> aad;

    void SetUp() override {
        // Use a fixed key for predictable tests. In real usage, key comes from KeyProvider.
        key.assign(SecureStorage::Crypto::AES_GCM_KEY_SIZE_BYTES, 0xAB); // 32 bytes of 0xAB
        plaintext = {'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!'};
        aad = {'S', 'o', 'm', 'e', 'A', 'A', 'D'};
    }
};

TEST_F(EncryptorTest, EncryptDecryptSuccess) {
    std::vector<unsigned char> encryptedData;
    SecureStorage::Error::Errc encResult = encryptor.encrypt(plaintext, key, encryptedData, aad);
    ASSERT_EQ(encResult, SecureStorage::Error::Errc::Success);
    ASSERT_FALSE(encryptedData.empty());
    ASSERT_GT(encryptedData.size(), plaintext.size()); // IV + Ciphertext + Tag

    std::vector<unsigned char> decryptedData;
    SecureStorage::Error::Errc decResult = encryptor.decrypt(encryptedData, key, decryptedData, aad);
    ASSERT_EQ(decResult, SecureStorage::Error::Errc::Success);
    ASSERT_EQ(decryptedData, plaintext);
}

TEST_F(EncryptorTest, EncryptDecryptNoAAD) {
    std::vector<unsigned char> encryptedData;
    SecureStorage::Error::Errc encResult = encryptor.encrypt(plaintext, key, encryptedData); // No AAD
    ASSERT_EQ(encResult, SecureStorage::Error::Errc::Success);
    ASSERT_FALSE(encryptedData.empty());

    std::vector<unsigned char> decryptedData;
    SecureStorage::Error::Errc decResult = encryptor.decrypt(encryptedData, key, decryptedData); // No AAD
    ASSERT_EQ(decResult, SecureStorage::Error::Errc::Success);
    ASSERT_EQ(decryptedData, plaintext);
}

TEST_F(EncryptorTest, DecryptWithWrongKey) {
    std::vector<unsigned char> encryptedData;
    ASSERT_EQ(encryptor.encrypt(plaintext, key, encryptedData, aad), SecureStorage::Error::Errc::Success);

    std::vector<unsigned char> wrongKey(SecureStorage::Crypto::AES_GCM_KEY_SIZE_BYTES, 0xCD);
    std::vector<unsigned char> decryptedData;
    SecureStorage::Error::Errc decResult = encryptor.decrypt(encryptedData, wrongKey, decryptedData, aad);
    // Expect AuthenticationFailed because GCM uses the key for tag verification
    ASSERT_EQ(decResult, SecureStorage::Error::Errc::AuthenticationFailed);
    ASSERT_TRUE(decryptedData.empty());
}

TEST_F(EncryptorTest, DecryptTamperedCiphertext) {
    std::vector<unsigned char> encryptedData;
    ASSERT_EQ(encryptor.encrypt(plaintext, key, encryptedData, aad), SecureStorage::Error::Errc::Success);

    // Tamper the ciphertext part (after IV, before Tag)
    if (encryptedData.size() > SecureStorage::Crypto::AES_GCM_IV_SIZE_BYTES + SecureStorage::Crypto::AES_GCM_TAG_SIZE_BYTES) {
        encryptedData[SecureStorage::Crypto::AES_GCM_IV_SIZE_BYTES]++; // Flip a bit
    } else if (encryptedData.size() > SecureStorage::Crypto::AES_GCM_IV_SIZE_BYTES) {
        // If plaintext was empty, ciphertext part is also empty.
        // To make this test meaningful for empty plaintext, we might need to tamper IV or Tag.
        // For non-empty plaintext, this tampering should work.
        if (plaintext.empty()) {
            SS_LOG_WARN("Plaintext is empty, tampering ciphertext has no effect on data itself.");
        }
    }


    std::vector<unsigned char> decryptedData;
    SecureStorage::Error::Errc decResult = encryptor.decrypt(encryptedData, key, decryptedData, aad);
    ASSERT_EQ(decResult, SecureStorage::Error::Errc::AuthenticationFailed);
}

TEST_F(EncryptorTest, DecryptTamperedTag) {
    std::vector<unsigned char> encryptedData;
    ASSERT_EQ(encryptor.encrypt(plaintext, key, encryptedData, aad), SecureStorage::Error::Errc::Success);

    // Tamper the tag part (last 16 bytes)
    if (encryptedData.size() >= SecureStorage::Crypto::AES_GCM_TAG_SIZE_BYTES) {
        encryptedData[encryptedData.size() - 1]++; // Flip a bit in the tag
    }

    std::vector<unsigned char> decryptedData;
    SecureStorage::Error::Errc decResult = encryptor.decrypt(encryptedData, key, decryptedData, aad);
    ASSERT_EQ(decResult, SecureStorage::Error::Errc::AuthenticationFailed);
}

TEST_F(EncryptorTest, DecryptTamperedIV) {
    std::vector<unsigned char> encryptedData;
    ASSERT_EQ(encryptor.encrypt(plaintext, key, encryptedData, aad), SecureStorage::Error::Errc::Success);

    // Tamper the IV part (first 12 bytes)
    if (encryptedData.size() >= SecureStorage::Crypto::AES_GCM_IV_SIZE_BYTES) {
        encryptedData[0]++; // Flip a bit in the IV
    }

    std::vector<unsigned char> decryptedData;
    SecureStorage::Error::Errc decResult = encryptor.decrypt(encryptedData, key, decryptedData, aad);
    ASSERT_EQ(decResult, SecureStorage::Error::Errc::AuthenticationFailed);
}


TEST_F(EncryptorTest, DecryptWithWrongAAD) {
    std::vector<unsigned char> encryptedData;
    ASSERT_EQ(encryptor.encrypt(plaintext, key, encryptedData, aad), SecureStorage::Error::Errc::Success);

    std::vector<unsigned char> wrongAad = {'D', 'i', 'f', 'f', 'A', 'A', 'D'};
    std::vector<unsigned char> decryptedData;
    SecureStorage::Error::Errc decResult = encryptor.decrypt(encryptedData, key, decryptedData, wrongAad);
    ASSERT_EQ(decResult, SecureStorage::Error::Errc::AuthenticationFailed);
}

TEST_F(EncryptorTest, EncryptEmptyPlaintext) {
    std::vector<unsigned char> emptyPlaintext;
    std::vector<unsigned char> encryptedData;
    SecureStorage::Error::Errc encResult = encryptor.encrypt(emptyPlaintext, key, encryptedData, aad);
    ASSERT_EQ(encResult, SecureStorage::Error::Errc::Success);
    // Output should be IV + Tag (since ciphertext is empty)
    ASSERT_EQ(encryptedData.size(), SecureStorage::Crypto::AES_GCM_IV_SIZE_BYTES + SecureStorage::Crypto::AES_GCM_TAG_SIZE_BYTES);

    std::vector<unsigned char> decryptedData;
    SecureStorage::Error::Errc decResult = encryptor.decrypt(encryptedData, key, decryptedData, aad);
    ASSERT_EQ(decResult, SecureStorage::Error::Errc::Success);
    ASSERT_TRUE(decryptedData.empty());
}

TEST_F(EncryptorTest, InvalidKeySize) {
    std::vector<unsigned char> shortKey(16, 0x01); // Too short
    std::vector<unsigned char> encryptedData;
    std::vector<unsigned char> decryptedData;

    SecureStorage::Error::Errc encResult = encryptor.encrypt(plaintext, shortKey, encryptedData, aad);
    ASSERT_EQ(encResult, SecureStorage::Error::Errc::InvalidKey);

    // Create some dummy encrypted data to test decrypt with short key
    encryptedData.resize(SecureStorage::Crypto::AES_GCM_IV_SIZE_BYTES + plaintext.size() + SecureStorage::Crypto::AES_GCM_TAG_SIZE_BYTES, 0xAA);
    SecureStorage::Error::Errc decResult = encryptor.decrypt(encryptedData, shortKey, decryptedData, aad);
    ASSERT_EQ(decResult, SecureStorage::Error::Errc::InvalidKey);
}

TEST_F(EncryptorTest, InputBufferTooSmallForDecrypt) {
    std::vector<unsigned char> tooSmallInput(SecureStorage::Crypto::AES_GCM_IV_SIZE_BYTES + SecureStorage::Crypto::AES_GCM_TAG_SIZE_BYTES - 1);
    std::vector<unsigned char> decryptedData;
    SecureStorage::Error::Errc decResult = encryptor.decrypt(tooSmallInput, key, decryptedData, aad);
    ASSERT_EQ(decResult, SecureStorage::Error::Errc::InvalidArgument);
}

TEST_F(EncryptorTest, MoveSemantics) {
    SecureStorage::Crypto::Encryptor e1("PersonalizationForMoveTest1");
    std::vector<unsigned char> enc1;
    ASSERT_EQ(e1.encrypt(plaintext, key, enc1, aad), SecureStorage::Error::Errc::Success);

    SecureStorage::Crypto::Encryptor e2(std::move(e1)); // Move constructor
    std::vector<unsigned char> dec1;
    ASSERT_EQ(e2.decrypt(enc1, key, dec1, aad), SecureStorage::Error::Errc::Success);
    ASSERT_EQ(dec1, plaintext);

    // e1 should be in a valid but moved-from state. Operations on it might fail or behave differently.
    // E.g., m_impl would be nullptr.
    // std::vector<unsigned char> enc1_after_move;
    // ASSERT_NE(e1.encrypt(plaintext, key, enc1_after_move, aad), SecureStorage::Error::Errc::Success);

    SecureStorage::Crypto::Encryptor e3("PersonalizationForMoveTest3");
    e3 = std::move(e2); // Move assignment
    std::vector<unsigned char> dec2;
    ASSERT_EQ(e3.decrypt(enc1, key, dec2, aad), SecureStorage::Error::Errc::Success);
    ASSERT_EQ(dec2, plaintext);
}