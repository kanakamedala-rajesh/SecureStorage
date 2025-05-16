#include "Error.h"

namespace SecureStorage {
namespace Error {

const char* SecureStorageErrorCategory::name() const noexcept {
    return "SecureStorage";
}

std::string SecureStorageErrorCategory::message(int condition) const {
    switch (static_cast<Errc>(condition)) {
        case Errc::Success:
            return "Success";
        case Errc::UnknownError:
            return "An unknown error occurred";
        case Errc::InvalidArgument:
            return "Invalid argument provided";
        case Errc::NotInitialized:
            return "Component or library not initialized";
        case Errc::OperationFailed:
            return "The requested operation failed";
        case Errc::FileOpenFailed:
            return "Failed to open file";
        case Errc::FileReadFailed:
            return "Failed to read from file";
        case Errc::FileWriteFailed:
            return "Failed to write to file";
        case Errc::FileRemoveFailed:
            return "Failed to remove file";
        case Errc::FileRenameFailed:
            return "Failed to rename file";
        case Errc::PathNotFound:
            return "Specified path not found";
        case Errc::AccessDenied:
            return "Access denied to file or resource";
        case Errc::EncryptionFailed:
            return "Data encryption failed";
        case Errc::DecryptionFailed:
            return "Data decryption failed";
        case Errc::AuthenticationFailed:
            return "Data authentication failed (e.g., GCM tag mismatch)";
        case Errc::KeyDerivationFailed:
            return "Encryption key derivation failed";
        case Errc::InvalidKey:
            return "Invalid encryption key";
        case Errc::InvalidIV:
            return "Invalid initialization vector (IV)";
        case Errc::CryptoLibraryError:
            return "Error occurred within the underlying crypto library";
        case Errc::DataNotFound:
            return "Requested data not found";
        case Errc::DataAlreadyExists:
            return "Data with the given identifier already exists";
        case Errc::SerializationFailed:
            return "Data serialization failed";
        case Errc::DeserializationFailed:
            return "Data deserialization failed";
        case Errc::WatcherStartFailed:
            return "File watcher failed to start";
        case Errc::WatcherReadFailed:
            return "Failed to read events from file watcher";
        case Errc::FileTampered:
            return "File watcher detected potential tampering";
        default:
            return "Unrecognized error code";
    }
}

const SecureStorageErrorCategory& SecureStorageErrorCategory::get() noexcept {
    static SecureStorageErrorCategory instance;
    return instance;
}

} // namespace Error
} // namespace SecureStorage