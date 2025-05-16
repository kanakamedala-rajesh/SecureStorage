#ifndef SS_KEY_PROVIDER_H
#define SS_KEY_PROVIDER_H

#include "Error.h" // For SecureStorage::Error::Errc
#include <string>
#include <vector>
#include <cstddef> // For size_t
#include <memory> // For std::unique_ptr

// Forward declare Mbed TLS types to avoid including Mbed TLS headers in our public header
// if they are only used in the .cpp. However, for key derivation, some contexts might be needed.
// For HKDF, mbedtls_md_context_t is often used internally.
// We will encapsulate Mbed TLS usage within the .cpp file.

namespace SecureStorage {
namespace Crypto {

// Constants for HKDF
// IMPORTANT: The HKDF_SALT should ideally be unique per application or product line.
// It does not need to be secret but should be fixed.
// For a real product, generate a random, fixed salt (e.g., 32 or 64 bytes)
// and store it as a byte array.
const std::string HKDF_SALT_DEFAULT = "DefaultSecureStorageAppSalt-V1"; // Example Salt
const std::string HKDF_INFO_DEFAULT = "SecureStorage-AES-256-GCM-Key-V1"; // Example Info

/**
 * @class KeyProvider
 * @brief Derives cryptographic keys using HKDF based on a device serial number.
 *
 * This class uses the HMAC-based Key Derivation Function (HKDF) as specified
 * in RFC 5869 to derive strong cryptographic keys from a given input
 * (device serial number) and a fixed salt and info string.
 */
class KeyProvider {
public:
    /**
     * @brief Constructs a KeyProvider.
     *
     * @param deviceSerialNumber The unique serial number of the device.
     * This will be used as the Input Keying Material (IKM) for HKDF.
     * @param salt A salt value for HKDF. If empty, a default salt is used.
     * It's recommended to use a specific, fixed salt for your application.
     * @param info An info string for HKDF context separation. If empty, a default info string is used.
     */
    explicit KeyProvider(
        std::string deviceSerialNumber,
        std::string salt = HKDF_SALT_DEFAULT,
        std::string info = HKDF_INFO_DEFAULT);

    ~KeyProvider(); // Required for pImpl or if Mbed TLS contexts are members

    KeyProvider(const KeyProvider&) = delete;
    KeyProvider& operator=(const KeyProvider&) = delete;
    KeyProvider(KeyProvider&&) noexcept; // Enable move semantics
    KeyProvider& operator=(KeyProvider&&) noexcept; // Enable move semantics

    /**
     * @brief Derives an encryption key of the specified length.
     *
     * @param[out] outputKey A vector to store the derived key. It will be resized appropriately.
     * @param keyLengthBytes The desired length of the key in bytes (e.g., 32 for AES-256).
     * @return SecureStorage::Error::Errc::Success on success, or an error code on failure.
     */
    Error::Errc getEncryptionKey(std::vector<unsigned char>& outputKey, size_t keyLengthBytes) const;

private:
    // Using PImpl pattern to hide Mbed TLS details and improve compilation times
    // and to manage Mbed TLS context lifetimes properly.
    class Impl;
    std::unique_ptr<Impl> m_impl;

    std::string m_deviceSerialNumber;
    std::string m_salt;
    std::string m_info;
};

} // namespace Crypto
} // namespace SecureStorage

#endif // SS_KEY_PROVIDER_H