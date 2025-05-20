#ifndef SECURE_STORAGE_H
#define SECURE_STORAGE_H

#include "utils/Error.h" // For SecureStorage::Error::Errc
#include <string>
#include <vector>
#include <memory> // For std::unique_ptr

// Forward declaration for PImpl if used, or for the internal SecureStore
namespace SecureStorage {
namespace Storage {
    class SecureStore; // Forward declare
}
// Forward declare FileWatcher if it were to be part of SecureStorageManager
// namespace Watcher { class FileWatcher; }

/**
 * @mainpage SecureStorage Library
 *
 * @section intro_sec Introduction
 * The SecureStorage library provides C++11 compatible mechanisms for encrypting,
 * decrypting, and securely storing data on disk, primarily targeted for
 * automotive custom Linux hardware and Android 18 based displays with
 * low memory and CPU footprints.
 *
 * It features:
 * - AES-256-GCM for authenticated encryption.
 * - Key derivation from a device-specific serial number using HKDF.
 * - Atomic file writes and backup strategies for data resilience.
 * - (Future) File watcher for monitoring unintended modifications.
 *
 * @section usage_sec Basic Usage
 * ```cpp
 * #include "SecureStorage.h" // This header
 * #include <iostream>
 *
 * int main() {
 * std::string root_path = "/tmp/my_secure_storage_data";
 * std::string serial_number = "DEVICE12345";
 *
 * SecureStorage::SecureStorageManager storageManager(root_path, serial_number);
 *
 * if (!storageManager.isInitialized()) {
 * std::cerr << "Failed to initialize SecureStorageManager!" << std::endl;
 * return 1;
 * }
 *
 * std::string dataId = "my_feature_settings";
 * std::vector<unsigned char> data_to_store = {'s', 'e', 'c', 'r', 'e', 't'};
 *
 * if (storageManager.storeData(dataId, data_to_store) == SecureStorage::Error::Errc::Success) {
 * std::cout << "Data stored successfully." << std::endl;
 * } else {
 * std::cerr << "Failed to store data." << std::endl;
 * }
 *
 * std::vector<unsigned char> retrieved_data;
 * if (storageManager.retrieveData(dataId, retrieved_data) == SecureStorage::Error::Errc::Success) {
 * std::cout << "Data retrieved: ";
 * for (unsigned char c : retrieved_data) { std::cout << c; }
 * std::cout << std::endl;
 * } else {
 * std::cerr << "Failed to retrieve data." << std::endl;
 * }
 * return 0;
 * }
 * ```
 */

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
     */
    SecureStorageManager(const std::string& rootStoragePath, const std::string& deviceSerialNumber);

    /**
     * @brief Destructor. Cleans up resources.
     */
    ~SecureStorageManager();

    // Disable copy operations as this manager handles unique underlying resources.
    SecureStorageManager(const SecureStorageManager&) = delete;
    SecureStorageManager& operator=(const SecureStorageManager&) = delete;

    // Enable move operations
    SecureStorageManager(SecureStorageManager&&) noexcept;
    SecureStorageManager& operator=(SecureStorageManager&&) noexcept;

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
    Error::Errc storeData(const std::string& data_id, const std::vector<unsigned char>& plain_data);

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
    Error::Errc retrieveData(const std::string& data_id, std::vector<unsigned char>& out_plain_data);

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
    Error::Errc deleteData(const std::string& data_id);

    /**
     * @brief Checks if data associated with a `data_id` exists in secure storage.
     *
     * @param data_id The unique identifier to check.
     * @return true if data for `data_id` exists, false otherwise or if not initialized/invalid ID.
     */
    bool dataExists(const std::string& data_id) const;

    /**
     * @brief Lists all unique data IDs currently stored.
     *
     * @param[out] out_data_ids A vector to be filled with the unique identifiers of
     * all stored data items. The vector is cleared first.
     * @return Error::Errc::Success on successful listing.
     * @return Error::Errc::NotInitialized if the manager is not initialized.
     * @return Other error codes for file system failures.
     */
    Error::Errc listDataIds(std::vector<std::string>& out_data_ids) const;

private:
    // Using PImpl to hide SecureStore and other potential future members
    // like FileWatcher, and to keep this public header clean.
    class SecureStorageManagerImpl;
    std::unique_ptr<SecureStorageManagerImpl> m_impl;
};

} // namespace SecureStorage

#endif // SECURE_STORAGE_H