#ifndef SS_ERROR_H
#define SS_ERROR_H

#include <system_error> // Required for std::error_code, std::error_category

namespace SecureStorage {
namespace Error {

/**
 * @enum Errc
 * @brief Defines specific error codes for the SecureStorage library.
 * These can be used with std::error_code.
 */
enum class Errc {
    Success = 0, // No error

    // General Errors
    UnknownError,
    InvalidArgument,
    NotInitialized,
    OperationFailed,

    // File System Errors
    FileOpenFailed,
    FileReadFailed,
    FileWriteFailed,
    FileRemoveFailed,
    FileRenameFailed,
    PathNotFound,
    AccessDenied,

    // Crypto Errors
    EncryptionFailed,
    DecryptionFailed,
    AuthenticationFailed, // e.g., GCM tag mismatch
    KeyDerivationFailed,
    InvalidKey,
    InvalidIV,
    CryptoLibraryError, // For errors from Mbed TLS

    // Data Storage Errors
    DataNotFound,
    DataAlreadyExists,
    SerializationFailed,
    DeserializationFailed,

    // File Watcher Errors
    WatcherStartFailed,
    WatcherReadFailed,
    FileTampered // Custom error if watcher detects unauthorized modification
};

/**
 * @brief The error category for SecureStorage library errors.
 *
 * This class provides the mapping from Errc enum values to error messages
 * and ensures that std::error_code can correctly represent library-specific errors.
 */
class SecureStorageErrorCategory : public std::error_category {
public:
    /**
     * @brief Returns the name of the error category.
     * @return The name "SecureStorage".
     */
    const char *name() const noexcept override;

    /**
     * @brief Converts an error code value (from Errc) into a descriptive string.
     * @param condition The integer value of the Errc enum.
     * @return A string describing the error.
     */
    std::string message(int condition) const override;

    /**
     * @brief Provides a singleton instance of the error category.
     * @return A reference to the static SecureStorageErrorCategory instance.
     */
    static const SecureStorageErrorCategory &get() noexcept;
};

/**
 * @brief Creates an std::error_code from an Errc value.
 * @param e The Errc value.
 * @return An std::error_code representing the library error.
 */
inline std::error_code make_error_code(Errc e) noexcept {
    return {static_cast<int>(e), SecureStorageErrorCategory::get()};
}

/**
 * @brief Creates an std::error_condition from an Errc value.
 * Used for comparing error conditions.
 * @param e The Errc value.
 * @return An std::error_condition representing the library error.
 */
inline std::error_condition make_error_condition(Errc e) noexcept {
    return {static_cast<int>(e), SecureStorageErrorCategory::get()};
}

} // namespace Error
} // namespace SecureStorage

namespace std {
/**
 * @brief Specialization of std::is_error_code_enum for SecureStorage::Error::Errc.
 *
 * This allows SecureStorage::Error::Errc to be used directly with
 * std::error_code mechanisms.
 */
template <> struct is_error_code_enum<SecureStorage::Error::Errc> : public true_type {};
} // namespace std

#endif // SS_ERROR_H