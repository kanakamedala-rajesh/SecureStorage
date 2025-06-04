#ifndef SECURE_STORAGE_H
#define SECURE_STORAGE_H

#include "file_watcher/FileWatcher.h" // For FileWatcher::EventCallback
#include "utils/Error.h"              // For SecureStorage::Error::Errc
#include <memory>                     // For std::unique_ptr
#include <string>
#include <vector>

/**
 * @mainpage SecureStorage Library Documentation
 *
 * @section intro_sec Introduction
 * The SecureStorage library provides robust C++11 compatible mechanisms for
 * encrypting, decrypting, and securely storing data on disk. It is designed
 * with a focus on low memory and CPU footprints, making it suitable for
 * resource-constrained environments such as automotive custom Linux hardware
 * and Android 18 based displays.
 *
 * The library ensures data confidentiality and integrity through authenticated
 * encryption and supports resilience against power key cycles.
 *
 * @section features_sec Key Features
 * - **Strong Encryption:** Utilizes AES-256-GCM for authenticated encryption, providing both
 * confidentiality and data integrity.
 * - **Device-Specific Keys:** Derives unique encryption keys for each device using its serial
 * number via HKDF (HMAC-based Key Derivation Function). Keys are not stored directly.
 * - **Secure Data Storage:** Manages encrypted data items within a specified root storage path.
 * - **Atomic Operations:** Employs atomic file write strategies (write-to-temp then rename) to
 * prevent data corruption during power loss or unexpected shutdowns.
 * - **Backup Strategy:** Maintains backup copies of encrypted data files for enhanced data
 * resilience.
 * - **File Watcher:** Continuously monitors encrypted files for unintended modifications and logs
 * these operations.
 * - **Cross-Platform Design:** Built with C++11 for cross-compilability on target Linux-based
 * systems.
 * - **Error Handling:** Provides clear error reporting via `SecureStorage::Error::Errc` and
 * `std::error_code`.
 * - **Low Footprint:** Designed to be mindful of memory and CPU usage.
 * - **Offline Operation:** Operates without requiring internet access.
 *
 * @section architecture_sec Architecture Overview
 * The library is composed of several key modules:
 * - **SecureStorageManager:** The main public facade providing a simplified API for library users.
 * - **SecureStore:** Handles the logical storage and retrieval of data items, managing encryption,
 * file I/O, and backups.
 * - **FileWatcher:** Monitors the storage directory for changes.
 * - **Encryptor & KeyProvider:** Core cryptographic components for AES-GCM encryption and key
 * derivation.
 * - **Utilities:** Helper classes for logging, error handling, and file operations.
 *
 * @section usage_sec Basic Usage
 * Include the main header and initialize the `SecureStorageManager`:
 * @code
 * #include "SecureStorage.h" // Main library header
 * #include <iostream>
 * #include <vector>
 * #include <string>
 *
 * int main() {
 * std::string root_path = "./my_app_secure_data"; // Choose an appropriate path
 * std::string device_serial = "012345678";    // Unique 9-digit device serial
 *
 * SecureStorage::SecureStorageManager manager(root_path, device_serial);
 *
 * if (!manager.isInitialized()) {
 * std::cerr << "Failed to initialize SecureStorageManager!" << std::endl;
 * return 1;
 * }
 * std::cout << "SecureStorageManager initialized. File watcher is "
 * << (manager.isFileWatcherActive() ? "active." : "inactive.") << std::endl;
 *
 * std::string data_id = "feature_X_config";
 * std::vector<unsigned char> my_data = {'s', 'e', 'c', 'r', 'e', 't', '!'};
 *
 * if (manager.storeData(data_id, my_data) == SecureStorage::Error::Errc::Success) {
 * std::cout << "Data stored for ID: " << data_id << std::endl;
 * }
 *
 * std::vector<unsigned char> retrieved_data;
 * if (manager.retrieveData(data_id, retrieved_data) == SecureStorage::Error::Errc::Success) {
 * std::cout << "Retrieved data for ID " << data_id << ": ";
 * for (unsigned char c : retrieved_data) { std::cout << c; }
 * std::cout << std::endl;
 * }
 * return 0;
 * }
 * @endcode
 *
 * @section building_sec Building the Library
 * The library uses CMake for building. Ensure Mbed TLS is available (it can be
 * fetched automatically via FetchContent).
 * @code
 * mkdir build && cd build
 * cmake ..
 * cmake --build .
 * @endcode
 *
 * @section notes_sec Important Notes
 * - The security of the stored data heavily relies on the uniqueness and secrecy of the device
 * serial number and the physical security of the device.
 * - The FileWatcher provides logging of filesystem events; application-level responses to these
 * events need to be implemented by the user if required beyond logging.
 */

namespace SecureStorage {
namespace Storage {
class SecureStore; // Forward declare
}
// Forward declare FileWatcher if it were to be part of SecureStorageManager
// namespace Watcher { class FileWatcher; }

/**
 * @class SecureStorageManager
 * @brief Main public interface for the SecureStorage library.
 *
 * This class provides a unified entry point for securely storing, retrieving,
 * and managing encrypted data. It encapsulates the underlying storage and
 * cryptographic mechanisms.
 */
class SecureStorageManager {
public:
    /**
     * @brief Constructs the SecureStorageManager.
     *
     * Initializes the secure storage system within the specified root path, using
     * the provided device serial number for master key derivation.
     *
     * @param rootStoragePath The absolute file system path where encrypted data
     * will be stored. This directory will be created if it
     * does not exist.
     * @param deviceSerialNumber A unique identifier for the device (e.g., a serial number)
     * used in the cryptographic key derivation process.
     * Must not be empty.
     * @param fileWatcherCallback An optional callback for file watcher events.
     * If nullptr, default logging by FileWatcher will occur.
     */
    SecureStorageManager(
        const std::string &storagePath,
        // const std::string &keyFilePath, // This was deviceSerialNumber, now removed
        FileWatcher::EventCallback callback);

    /**
     * @brief Destructor. Cleans up resources.
     */
    ~SecureStorageManager();

    // Disable copy operations as this manager handles unique underlying resources.
    SecureStorageManager(const SecureStorageManager &) = delete;
    SecureStorageManager &operator=(const SecureStorageManager &) = delete;

    // Enable move operations
    SecureStorageManager(SecureStorageManager &&) noexcept;
    SecureStorageManager &operator=(SecureStorageManager &&) noexcept;

    /**
     * @brief Checks if the SecureStorageManager was successfully initialized.
     *
     * Initialization involves setting up the storage path and deriving necessary
     * cryptographic keys. Operations should only be attempted if this returns true.
     *
     * @return true if the manager is properly initialized and ready for use, false otherwise.
     */
    bool isInitialized() const;

    /**
     * @brief Securely stores a piece of data.
     *
     * The provided data is encrypted using the device-derived master key and
     * stored in a file uniquely identified by `data_id`.
     *
     * @param data_id A unique string identifier for the data item. This ID will be
     * used for retrieval and deletion. It should not contain path
     * separators (e.g., '/', '\') or be empty.
     * @param plain_data A vector of bytes representing the data to be stored.
     * @return Error::Errc::Success on successful storage.
     * @return Error::Errc::NotInitialized if the manager is not initialized.
     * @return Error::Errc::InvalidArgument if `data_id` is invalid.
     * @return Other error codes for encryption or file system failures.
     */
    Error::Errc storeData(const std::string &data_id, const std::vector<unsigned char> &plain_data);

    /**
     * @brief Retrieves securely stored data.
     *
     * Decrypts and returns the data associated with the given `data_id`.
     * The system will attempt to use backup files if the primary data file is
     * corrupted or missing.
     *
     * @param data_id The unique identifier of the data to retrieve.
     * @param[out] out_plain_data A vector where the decrypted data will be stored.
     * It will be cleared if retrieval fails.
     * @return Error::Errc::Success on successful retrieval and decryption.
     * @return Error::Errc::NotInitialized if the manager is not initialized.
     * @return Error::Errc::InvalidArgument if `data_id` is invalid.
     * @return Error::Errc::DataNotFound if no data exists for `data_id`.
     * @return Error::Errc::AuthenticationFailed if data integrity check fails (tampering).
     * @return Other error codes for decryption or file system failures.
     */
    Error::Errc retrieveData(const std::string &data_id,
                             std::vector<unsigned char> &out_plain_data);

    /**
     * @brief Deletes securely stored data.
     *
     * Removes the data file (and its backup) associated with the `data_id`.
     *
     * @param data_id The unique identifier of the data to delete.
     * @return Error::Errc::Success if deletion was successful or if data did not exist.
     * @return Error::Errc::NotInitialized if the manager is not initialized.
     * @return Error::Errc::InvalidArgument if `data_id` is invalid.
     * @return Other error codes for file system failures.
     */
    Error::Errc deleteData(const std::string &data_id);

    /**
     * @brief Checks if data associated with a `data_id` exists in secure storage.
     *
     * @param data_id The unique identifier to check.
     * @return true if data for `data_id` exists, false otherwise or if not initialized/invalid ID.
     */
    bool dataExists(const std::string &data_id) const;

    /**
     * @brief Lists all unique data IDs currently stored.
     *
     * @param[out] out_data_ids A vector to be filled with the unique identifiers of
     * all stored data items. The vector is cleared first.
     * @return Error::Errc::Success on successful listing.
     * @return Error::Errc::NotInitialized if the manager is not initialized.
     * @return Other error codes for file system failures.
     */
    Error::Errc listDataIds(std::vector<std::string> &out_data_ids) const;

    /**
     * @brief Checks if the file watcher component is active.
     *
     * The file watcher monitors the storage directory for external changes.
     * This method indicates if the watcher was successfully initialized and started.
     * Note that the SecureStorageManager itself might be `isInitialized()` for storage
     * operations even if the file watcher failed to start.
     *
     * @return true if the file watcher is active, false otherwise.
     */
    bool isFileWatcherActive() const;

private:
    // Using PImpl to hide SecureStore and other potential future members
    // like FileWatcher, and to keep this public header clean.
    class SecureStorageManagerImpl;
    std::unique_ptr<SecureStorageManagerImpl> m_impl;

    // Member variable declaration (around line 98)
    // This line should now compile correctly after adding the include for FileWatcher
    FileWatcher::EventCallback fileWatcherCallback =
        nullptr; // Optional callback for file watcher events
};

} // namespace SecureStorage

#endif // SECURE_STORAGE_H