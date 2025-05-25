#include "Encryptor.h"
#include "Logger.h" // For SS_LOG_ macros (using SFS_LOG for now)
#include <cstring>  // For memcpy, memset
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h> // For mbedtls_strerror
#include <mbedtls/gcm.h>

namespace SecureStorage {
namespace Crypto {

// Definition of the PImpl class for Encryptor
class Encryptor::Impl {
public:
    mbedtls_gcm_context gcm_ctx;
    mbedtls_ctr_drbg_context drbg_ctx;
    mbedtls_entropy_context entropy_ctx;
    bool initialized;

    Impl(const std::string &personalizationData) : initialized(false) {
        mbedtls_gcm_init(&gcm_ctx);
        mbedtls_ctr_drbg_init(&drbg_ctx);
        mbedtls_entropy_init(&entropy_ctx);

        // Seed the random number generator
        int ret = mbedtls_ctr_drbg_seed(
            &drbg_ctx, mbedtls_entropy_func, &entropy_ctx,
            reinterpret_cast<const unsigned char *>(personalizationData.data()),
            personalizationData.length());

        if (ret != 0) {
            char error_buf[100];
            mbedtls_strerror(ret, error_buf, sizeof(error_buf));
            SS_LOG_ERROR("Failed to seed CTR_DRBG: " << error_buf);
            // cleanup in destructor will still happen
            return;
        }
        initialized = true;
        SS_LOG_DEBUG("Encryptor Impl initialized and RNG seeded.");
    }

    ~Impl() {
        mbedtls_gcm_free(&gcm_ctx);
        mbedtls_ctr_drbg_free(&drbg_ctx);
        mbedtls_entropy_free(&entropy_ctx);
        SS_LOG_DEBUG("Encryptor Impl cleaned up Mbed TLS contexts.");
    }
};

Encryptor::Encryptor(const std::string &personalizationData)
    : m_impl(new Impl(personalizationData)) {
    if (!m_impl->initialized) {
        SS_LOG_ERROR("Encryptor construction failed due to RNG seeding failure.");
        // This state should ideally be signaled, e.g., by throwing an exception
        // or having an is_valid() method. For now, operations will fail.
    }
}

Encryptor::~Encryptor() = default; // Handles m_impl cleanup

// Move constructor
Encryptor::Encryptor(Encryptor &&other) noexcept : m_impl(std::move(other.m_impl)) {
}

// Move assignment
Encryptor &Encryptor::operator=(Encryptor &&other) noexcept {
    if (this != &other) {
        m_impl = std::move(other.m_impl);
    }
    return *this;
}

Error::Errc Encryptor::generateIv(std::vector<unsigned char> &iv) {
    if (!m_impl || !m_impl->initialized) {
        SS_LOG_ERROR("RNG not initialized for IV generation.");
        return Error::Errc::NotInitialized;
    }
    iv.resize(AES_GCM_IV_SIZE_BYTES);
    int ret = mbedtls_ctr_drbg_random(&m_impl->drbg_ctx, iv.data(), iv.size());
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        SS_LOG_ERROR("Failed to generate IV: " << error_buf);
        return Error::Errc::CryptoLibraryError;
    }
    return Error::Errc::Success;
}

Error::Errc Encryptor::encrypt(const std::vector<unsigned char> &plaintext,
                               const std::vector<unsigned char> &key,
                               std::vector<unsigned char> &outputBuffer,
                               const std::vector<unsigned char> &aad) {

    if (!m_impl || !m_impl->initialized) {
        SS_LOG_ERROR("Encryptor not properly initialized.");
        return Error::Errc::NotInitialized;
    }
    if (key.size() != AES_GCM_KEY_SIZE_BYTES) {
        SS_LOG_ERROR("Invalid key size for AES-256-GCM. Expected " << AES_GCM_KEY_SIZE_BYTES
                                                                   << " bytes, got " << key.size());
        return Error::Errc::InvalidKey;
    }

    std::vector<unsigned char> iv;
    Error::Errc iv_err = generateIv(iv);
    if (iv_err != Error::Errc::Success) {
        return iv_err;
    }

    // Prepare output buffer: IV + Ciphertext + Tag
    // Ciphertext length is same as plaintext for GCM.
    outputBuffer.resize(AES_GCM_IV_SIZE_BYTES + plaintext.size() + AES_GCM_TAG_SIZE_BYTES);

    // Pointers to different parts of the outputBuffer
    unsigned char *iv_ptr = outputBuffer.data();
    unsigned char *ciphertext_ptr = outputBuffer.data() + AES_GCM_IV_SIZE_BYTES;
    unsigned char *tag_ptr = outputBuffer.data() + AES_GCM_IV_SIZE_BYTES + plaintext.size();

    // Copy IV to the beginning of the output buffer
    std::memcpy(iv_ptr, iv.data(), AES_GCM_IV_SIZE_BYTES);

    int ret =
        mbedtls_gcm_setkey(&m_impl->gcm_ctx, MBEDTLS_CIPHER_ID_AES, key.data(), key.size() * 8);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        SS_LOG_ERROR("mbedtls_gcm_setkey failed: " << error_buf);
        return Error::Errc::CryptoLibraryError;
    }

    // Perform encryption and generate tag
    ret = mbedtls_gcm_crypt_and_tag(&m_impl->gcm_ctx, MBEDTLS_GCM_ENCRYPT, plaintext.size(),
                                    iv.data(), iv.size(), aad.empty() ? nullptr : aad.data(),
                                    aad.size(), plaintext.empty() ? nullptr : plaintext.data(),
                                    ciphertext_ptr,                 // Output ciphertext
                                    AES_GCM_TAG_SIZE_BYTES, tag_ptr // Output tag
    );

    mbedtls_gcm_free(
        &m_impl->gcm_ctx); // Important to free and re-init if using context for multiple operations
    mbedtls_gcm_init(
        &m_impl->gcm_ctx); // Re-initialize for next operation (or manage state carefully)

    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        SS_LOG_ERROR("mbedtls_gcm_crypt_and_tag (encrypt) failed: " << error_buf);
        outputBuffer.clear(); // Clear output on failure
        return Error::Errc::EncryptionFailed;
    }

    SS_LOG_DEBUG("Encryption successful. Output size: " << outputBuffer.size());
    return Error::Errc::Success;
}

Error::Errc Encryptor::decrypt(const std::vector<unsigned char> &inputBuffer,
                               const std::vector<unsigned char> &key,
                               std::vector<unsigned char> &plaintext,
                               const std::vector<unsigned char> &aad) {

    if (!m_impl || !m_impl->initialized) {
        SS_LOG_ERROR("Encryptor not properly initialized.");
        return Error::Errc::NotInitialized;
    }
    if (key.size() != AES_GCM_KEY_SIZE_BYTES) {
        SS_LOG_ERROR("Invalid key size for AES-256-GCM decryption. Expected "
                     << AES_GCM_KEY_SIZE_BYTES << " bytes, got " << key.size());
        return Error::Errc::InvalidKey;
    }
    if (inputBuffer.size() < AES_GCM_IV_SIZE_BYTES + AES_GCM_TAG_SIZE_BYTES) {
        SS_LOG_ERROR("Input buffer too small for IV and Tag. Size: " << inputBuffer.size());
        return Error::Errc::InvalidArgument;
    }

    const unsigned char *iv_ptr = inputBuffer.data();
    const unsigned char *ciphertext_ptr = inputBuffer.data() + AES_GCM_IV_SIZE_BYTES;
    size_t ciphertext_len = inputBuffer.size() - AES_GCM_IV_SIZE_BYTES - AES_GCM_TAG_SIZE_BYTES;
    const unsigned char *tag_ptr = inputBuffer.data() + AES_GCM_IV_SIZE_BYTES + ciphertext_len;

    plaintext.resize(ciphertext_len);

    int ret =
        mbedtls_gcm_setkey(&m_impl->gcm_ctx, MBEDTLS_CIPHER_ID_AES, key.data(), key.size() * 8);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        SS_LOG_ERROR("mbedtls_gcm_setkey failed (decrypt): " << error_buf);
        return Error::Errc::CryptoLibraryError;
    }

    // Perform decryption and authentication
    ret = mbedtls_gcm_auth_decrypt(
        &m_impl->gcm_ctx, ciphertext_len, iv_ptr, AES_GCM_IV_SIZE_BYTES,
        aad.empty() ? nullptr : aad.data(), aad.size(), tag_ptr, AES_GCM_TAG_SIZE_BYTES,
        ciphertext_ptr,
        plaintext.empty() && ciphertext_len == 0 ? nullptr : plaintext.data() // Output plaintext
    );

    mbedtls_gcm_free(
        &m_impl->gcm_ctx); // Important to free and re-init if using context for multiple operations
    mbedtls_gcm_init(&m_impl->gcm_ctx); // Re-initialize for next operation

    if (ret != 0) {
        plaintext.clear(); // Clear output on failure
        if (ret == MBEDTLS_ERR_GCM_AUTH_FAILED) {
            SS_LOG_WARN(
                "GCM authentication failed during decryption (tag mismatch or tampered data).");
            return Error::Errc::AuthenticationFailed;
        } else {
            char error_buf[100];
            mbedtls_strerror(ret, error_buf, sizeof(error_buf));
            SS_LOG_ERROR("mbedtls_gcm_auth_decrypt failed: " << error_buf);
            return Error::Errc::DecryptionFailed;
        }
    }

    SS_LOG_DEBUG("Decryption successful. Plaintext size: " << plaintext.size());
    return Error::Errc::Success;
}

} // namespace Crypto
} // namespace SecureStorage