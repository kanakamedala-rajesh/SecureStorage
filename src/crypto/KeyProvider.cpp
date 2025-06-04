#include "KeyProvider.h"
#include "Logger.h"        // For SS_LOG_ERROR, SS_LOG_DEBUG (adjust to SS_LOG_*)
#include "utils/ISystemIdProvider.h" // Added
#include <mbedtls/error.h> // For mbedtls_strerror
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h> // For mbedtls_md_info_from_type
#include <memory>       // For std::unique_ptr

namespace SecureStorage {
namespace Crypto {

// Definition of the PImpl class
class KeyProvider::Impl {
public:
    // Mbed TLS contexts or other private members can go here if needed.
    // For HKDF, mbedtls_hkdf itself doesn't require a persistent context object
    // between calls if all parameters are passed each time.
    // However, if we were using HMAC directly, we might store an mbedtls_md_context_t.
    // For now, Impl is empty as mbedtls_hkdf is stateless given its inputs.
    Impl() = default;
    ~Impl() = default; // Ensure proper cleanup if contexts were added
};

KeyProvider::KeyProvider(const Utils::ISystemIdProvider& systemIdProvider, // Changed
                         std::string salt, std::string info)
    : m_impl(new Impl()), m_systemIdProvider(systemIdProvider), // Changed
      m_salt(std::move(salt)), m_info(std::move(info)) {
    // Removed check for m_deviceSerialNumber.empty()
    // The systemIdProvider is responsible for providing a valid ID.
    // Error handling for getSystemId will be in getEncryptionKey.
    if (m_salt.empty()) {
        SS_LOG_WARN("KeyProvider using an empty salt. Default was likely intended.");
        m_salt = HKDF_SALT_DEFAULT;
    }
    if (m_info.empty()) {
        SS_LOG_WARN("KeyProvider using an empty info string. Default was likely intended.");
        m_info = HKDF_INFO_DEFAULT;
    }
}

KeyProvider::~KeyProvider() = default;

// Move constructor
KeyProvider::KeyProvider(KeyProvider &&other) noexcept
    : m_impl(std::move(other.m_impl)),
      m_systemIdProvider(other.m_systemIdProvider), // References are copied
      m_salt(std::move(other.m_salt)),
      m_info(std::move(other.m_info)) {
}

// Move assignment operator
KeyProvider &KeyProvider::operator=(KeyProvider &&other) noexcept {
    if (this != &other) {
        m_impl = std::move(other.m_impl);
        // m_systemIdProvider = other.m_systemIdProvider; // This line would be problematic for references.
        // Re-assigning a reference member is not possible.
        // This implies KeyProvider might not be ideally suited for move assignment
        // if it holds a raw reference and its lifetime/ownership model requires frequent moves.
        // For now, we assume it's constructed in place or moved carefully.
        // To make it properly move-assignable with a reference, one might use
        // std::reference_wrapper or reconsider the design if KeyProvider instances
        // themselves need to be assigned after construction.
        // However, the constructor takes a const ref, and if the provider's lifetime
        // outlives the KeyProvider or is managed correctly, this is okay.
        // Let's proceed with the assumption that KeyProvider instances are not typically reassigned this way.
        // If they are, this part of the design would need refinement (e.g. using std::reference_wrapper
        // or changing ownership semantics).

        // A common pattern for classes holding references is to delete move assignment
        // or make it private if it doesn't make sense.
        // For now, let's assume the provided move operations are sufficient given its usage context.
        // The critical part is that `other.m_systemIdProvider` must remain valid.
        // This is generally true if `other` is an expiring object.

        m_salt = std::move(other.m_salt);
        m_info = std::move(other.m_info);
    }
    return *this;
}


Error::Errc KeyProvider::getEncryptionKey(std::vector<unsigned char> &outputKey,
                                          size_t keyLengthBytes) const {
    std::string systemId;
    Error::Errc idErr = m_systemIdProvider.getSystemId(systemId);
    if (idErr != Error::Errc::Success) {
        SS_LOG_ERROR("Failed to retrieve system ID: " << Error::GetErrorMessage(idErr));
        return idErr;
    }
    if (systemId.empty()) {
        SS_LOG_ERROR("Cannot derive key: System ID is empty.");
        return Error::Errc::InvalidArgument; // Or a more specific error
    }

    if (keyLengthBytes == 0) {
        SS_LOG_ERROR("Cannot derive key: Requested key length is 0.");
        return Error::Errc::InvalidArgument;
    }

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == nullptr) {
        SS_LOG_ERROR("Failed to get SHA256 message digest info from Mbed TLS.");
        return Error::Errc::CryptoLibraryError;
    }

    outputKey.resize(keyLengthBytes);

    // IKM: Input Keying Material (now systemId)
    const unsigned char *ikm = reinterpret_cast<const unsigned char *>(systemId.data());
    size_t ikm_len = systemId.length();

    // Salt
    const unsigned char *salt_ptr = reinterpret_cast<const unsigned char *>(m_salt.data());
    size_t salt_len = m_salt.length();

    // Info
    const unsigned char *info_ptr = reinterpret_cast<const unsigned char *>(m_info.data());
    size_t info_len = m_info.length();

    SS_LOG_DEBUG("Deriving key with HKDF: IKM (SystemID)_len=" << ikm_len
                                                    << ", Salt_len=" << salt_len
                                                    << ", Info_len=" << info_len
                                                    << ", OutputKey_len=" << keyLengthBytes);

    int ret = mbedtls_hkdf(md_info, salt_ptr, salt_len, ikm, ikm_len, info_ptr, info_len,
                           outputKey.data(), keyLengthBytes);

    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        SS_LOG_ERROR("Mbed TLS HKDF failed: " << error_buf << " (Code: " << ret << ")");
        outputKey.clear(); // Ensure no partial key is exposed on failure
        return Error::Errc::KeyDerivationFailed;
    }

    SS_LOG_DEBUG("Successfully derived " << keyLengthBytes << "-byte key using HKDF with System ID.");
    return Error::Errc::Success;
}

} // namespace Crypto
} // namespace SecureStorage