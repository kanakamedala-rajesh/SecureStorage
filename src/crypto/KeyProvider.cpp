#include "KeyProvider.h"
#include "Logger.h" // For SS_LOG_ERROR, SS_LOG_DEBUG (adjust to SS_LOG_*)
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>      // For mbedtls_md_info_from_type
#include <mbedtls/error.h>   // For mbedtls_strerror
#include <memory>            // For std::unique_ptr
#include <utility>           // For std::move
#include "FileUtil.h"        // For readFile

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

KeyProvider::KeyProvider(std::string deviceSerialNumber, std::string deviceSecretFilePath, std::string salt, std::string info)
    : m_impl(new Impl()),
      m_deviceSerialNumber(std::move(deviceSerialNumber)),
      m_deviceSecretFilePath(std::move(deviceSecretFilePath)),
      m_salt(std::move(salt)),
      m_info(std::move(info)) {
    if (m_deviceSerialNumber.empty()) {
        SS_LOG_ERROR("KeyProvider initialized with an empty device serial number.");
    }
    if (m_deviceSecretFilePath.empty()) {
        SS_LOG_ERROR("KeyProvider initialized with an empty device secret file path.");
    }
    if (m_salt.empty()) {
        SS_LOG_WARN("KeyProvider using an empty salt. This is not recommended. Default was likely intended.");
        m_salt = HKDF_SALT_DEFAULT; // Fallback just in case
    }
     if (m_info.empty()) {
        SS_LOG_WARN("KeyProvider using an empty info string. Default was likely intended.");
        m_info = HKDF_INFO_DEFAULT; // Fallback just in case
    }
}

KeyProvider::~KeyProvider() = default; // Needed for std::unique_ptr<Impl>

// Move constructor
KeyProvider::KeyProvider(KeyProvider&& other) noexcept
    : m_impl(std::move(other.m_impl)),
      m_deviceSerialNumber(std::move(other.m_deviceSerialNumber)),
      m_deviceSecretFilePath(std::move(other.m_deviceSecretFilePath)),
      m_salt(std::move(other.m_salt)),
      m_info(std::move(other.m_info)) {}

// Move assignment operator
KeyProvider& KeyProvider::operator=(KeyProvider&& other) noexcept {
    if (this != &other) {
        m_impl = std::move(other.m_impl);
        m_deviceSerialNumber = std::move(other.m_deviceSerialNumber);
        m_deviceSecretFilePath = std::move(other.m_deviceSecretFilePath);
        m_salt = std::move(other.m_salt);
        m_info = std::move(other.m_info);
    }
    return *this;
}


Error::Errc KeyProvider::getEncryptionKey(std::vector<unsigned char>& outputKey, size_t keyLengthBytes) const {
    if (m_deviceSerialNumber.empty()) {
        SS_LOG_ERROR("Cannot derive key: Device serial number is empty.");
        return Error::Errc::InvalidArgument;
    }
    if (m_deviceSecretFilePath.empty()) {
        SS_LOG_ERROR("Cannot derive key: Device secret file path is empty.");
        return Error::Errc::InvalidArgument;
    }
    if (keyLengthBytes == 0) {
        SS_LOG_ERROR("Cannot derive key: Requested key length is 0.");
        return Error::Errc::InvalidArgument;
    }

    // Read the device secret from the file
    std::string deviceSecret;
    Error::Errc readErr = Utils::FileUtil::readFileIntoString(m_deviceSecretFilePath, deviceSecret);
    if (readErr != Error::Errc::Success) {
        SS_LOG_ERROR("Failed to read device secret file: " << m_deviceSecretFilePath);
        return readErr;
    }

    if (deviceSecret.empty()) {
        SS_LOG_ERROR("Device secret file is empty: " << m_deviceSecretFilePath);
        return Error::Errc::InvalidFile;
    }

    // Combine serial number and device secret to form the IKM
    std::string ikm_material = m_deviceSerialNumber + deviceSecret;

    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == nullptr) {
        SS_LOG_ERROR("Failed to get SHA256 message digest info from Mbed TLS.");
        return Error::Errc::CryptoLibraryError;
    }

    outputKey.resize(keyLengthBytes);

    // IKM: Input Keying Material (device serial number + device secret)
    const unsigned char* ikm = reinterpret_cast<const unsigned char*>(ikm_material.data());
    size_t ikm_len = ikm_material.length();

    // Salt
    const unsigned char* salt_ptr = reinterpret_cast<const unsigned char*>(m_salt.data());
    size_t salt_len = m_salt.length();

    // Info
    const unsigned char* info_ptr = reinterpret_cast<const unsigned char*>(m_info.data());
    size_t info_len = m_info.length();

    SS_LOG_DEBUG("Deriving key with HKDF: IKM_len=" << ikm_len
        << ", Salt_len=" << salt_len << ", Info_len=" << info_len
        << ", OutputKey_len=" << keyLengthBytes);

    int ret = mbedtls_hkdf(
        md_info,
        salt_ptr, salt_len,
        ikm, ikm_len,
        info_ptr, info_len,
        outputKey.data(), keyLengthBytes
    );

    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        SS_LOG_ERROR("Mbed TLS HKDF failed: " << error_buf << " (Code: " << ret << ")");
        outputKey.clear(); // Ensure no partial key is exposed on failure
        return Error::Errc::KeyDerivationFailed;
    }

    SS_LOG_DEBUG("Successfully derived " << keyLengthBytes << "-byte key using HKDF.");
    return Error::Errc::Success;
}

} // namespace Crypto
} // namespace SecureStorage