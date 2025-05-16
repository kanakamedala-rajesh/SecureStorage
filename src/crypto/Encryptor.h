#ifndef SS_ENCRYPTOR_H
#define SS_ENCRYPTOR_H

#include "Error.h"
#include <vector>
#include <string>
#include <memory> // For std::unique_ptr

// Forward declare Mbed TLS types to keep them out of this public header
struct mbedtls_gcm_context;
struct mbedtls_ctr_drbg_context;
struct mbedtls_entropy_context;

namespace SecureStorage {
namespace Crypto {

// Standard AES-GCM constants
constexpr size_t AES_GCM_KEY_SIZE_BYTES = 32; // 256 bits
constexpr size_t AES_GCM_IV_SIZE_BYTES = 12;  // 96 bits is optimal for GCM
constexpr size_t AES_GCM_TAG_SIZE_BYTES = 16; // 128 bits tag

/**
 * @class Encryptor
 * @brief Provides AES-256-GCM encryption and decryption services.
 *
 * This class handles authenticated encryption using AES in Galois/Counter Mode (GCM).
 * It manages Mbed TLS contexts for GCM operations and random IV generation.
 * Each encryption operation generates a unique IV. The output format is:
 * [IV (12 bytes)] + [Ciphertext] + [Authentication Tag (16 bytes)]
 */
class Encryptor {
public:
    /**
     * @brief Constructs an Encryptor instance.
     * Initializes contexts for AES-GCM and random number generation.
     * @param personalizationData A string used to seed the random number generator.
     * This should ideally be unique per application/device instance.
     * "SecureStorageEncryptor" can be a default.
     */
    explicit Encryptor(const std::string& personalizationData = "SecureStorageEncryptorSeed");
    ~Encryptor();

    Encryptor(const Encryptor&) = delete;
    Encryptor& operator=(const Encryptor&) = delete;
    Encryptor(Encryptor&&) noexcept;
    Encryptor& operator=(Encryptor&&) noexcept;

    /**
     * @brief Encrypts plaintext data using AES-256-GCM.
     *
     * A unique 12-byte IV is generated for each encryption.
     * The output format is [IV] + [Ciphertext] + [Tag].
     *
     * @param plaintext The data to encrypt.
     * @param key The 256-bit (32-byte) encryption key.
     * @param[out] outputBuffer Vector to store the concatenated IV, ciphertext, and GCM tag.
     * It will be resized appropriately.
     * @param aad Optional Additional Authenticated Data (AAD). This data is authenticated
     * but not encrypted. Default is empty.
     * @return SecureStorage::Error::Errc::Success on success, or an error code on failure.
     */
    Error::Errc encrypt(
        const std::vector<unsigned char>& plaintext,
        const std::vector<unsigned char>& key,
        std::vector<unsigned char>& outputBuffer,
        const std::vector<unsigned char>& aad = {});

    /**
     * @brief Decrypts data previously encrypted with AES-256-GCM.
     *
     * Expects input format: [IV (12 bytes)] + [Ciphertext] + [Tag (16 bytes)].
     *
     * @param inputBuffer The data containing IV, ciphertext, and GCM tag.
     * @param key The 256-bit (32-byte) encryption key.
     * @param[out] plaintext Vector to store the decrypted data.
     * It will be resized appropriately.
     * @param aad Optional Additional Authenticated Data (AAD) that was used during encryption.
     * Must match the AAD used during encryption. Default is empty.
     * @return SecureStorage::Error::Errc::Success on success.
     * SecureStorage::Error::Errc::AuthenticationFailed if GCM tag mismatch or data tampered.
     * Other error codes on different failures.
     */
    Error::Errc decrypt(
        const std::vector<unsigned char>& inputBuffer,
        const std::vector<unsigned char>& key,
        std::vector<unsigned char>& plaintext,
        const std::vector<unsigned char>& aad = {});

private:
    // PImpl idiom to hide Mbed TLS context details
    class Impl;
    std::unique_ptr<Impl> m_impl;

    /**
     * @brief Generates a random Initialization Vector (IV).
     * @param[out] iv Vector to store the generated IV. It will be sized to AES_GCM_IV_SIZE_BYTES.
     * @return SecureStorage::Error::Errc::Success on success, or an error code.
     */
    Error::Errc generateIv(std::vector<unsigned char>& iv);
};

} // namespace Crypto
} // namespace SecureStorage

#endif // SS_ENCRYPTOR_H