#ifndef SS_SECURE_STORE_H
#define SS_SECURE_STORE_H

#include "Encryptor.h"
#include "Error.h"
#include "FileUtil.h" // For filename suffix constants if any
#include "KeyProvider.h"
#include <memory> // For std::unique_ptr
#include <string>
#include <vector>

namespace SecureStorage {
namespace Storage {

// Define file extensions consistently
const std::string DATA_FILE_EXTENSION = ".enc";
const std::string BACKUP_FILE_EXTENSION = ".bak";
const std::string TEMP_FILE_SUFFIX = ".tmp";

/**
 * @class SecureStore
 * @brief Manages secure storage and retrieval of encrypted data items in files.
 *
 * This class uses a KeyProvider to derive a master encryption key based on a
 * device serial number, and an Encryptor to perform AES-256-GCM encryption.
 * Data items are stored as individual encrypted files within a specified root path.
 * Includes a backup mechanism for resilience.
 */
class SecureStore {
public:
    /**
     * @brief Constructs a SecureStore instance.
     *
     * @param rootStoragePath The absolute path to the directory where encrypted files will be
     * stored. This directory will be created if it doesn't exist.
     * @param deviceSerialNumber The unique serial number of the device, used for key derivation.
     */
    SecureStore(std::string rootStoragePath, std::string deviceSerialNumber);

    ~SecureStore() = default;

    // Disable copy and assignment as this class manages unique resources (files, potentially
    // contexts)
    SecureStore(const SecureStore &) = delete;
    SecureStore &operator=(const SecureStore &) = delete;
    // Move semantics can be considered if complex internal state needs efficient transfer
    SecureStore(SecureStore &&) = delete;            // Simpler to disallow for now
    SecureStore &operator=(SecureStore &&) = delete; // Simpler to disallow for now

    /**
     * @brief Checks if the SecureStore was initialized successfully.
     * Initialization includes setting up the root path and deriving the master key.
     * @return true if initialized successfully, false otherwise.
     */
    bool isInitialized() const;

    /**
     * @brief Stores a data item securely.
     * The data is encrypted and written to a file named after the data_id.
     * An existing backup becomes the old backup, an existing main file becomes the new backup.
     *
     * @param data_id A unique identifier for the data item. Used to name the file.
     * Should not contain path separators or be empty.
     * @param plain_data The raw data to be stored and encrypted.
     * @return SecureStorage::Error::Errc::Success on success, or an error code on failure.
     */
    Error::Errc storeData(const std::string &data_id, const std::vector<unsigned char> &plain_data);

    /**
     * @brief Retrieves a securely stored data item.
     * Attempts to read from the main data file first. If that fails (missing, corrupt),
     * it attempts to read from the backup file. If the backup is used and successfully
     * decrypted, an attempt is made to restore it to the main file.
     *
     * @param data_id The unique identifier of the data item to retrieve.
     * @param[out] out_plain_data A vector to store the decrypted data.
     * @return SecureStorage::Error::Errc::Success on success, or an error code on failure
     * (e.g., DataNotFound, DecryptionFailed, AuthenticationFailed).
     */
    Error::Errc retrieveData(const std::string &data_id,
                             std::vector<unsigned char> &out_plain_data);

    /**
     * @brief Deletes a securely stored data item.
     * Removes both the main data file and its backup.
     *
     * @param data_id The unique identifier of the data item to delete.
     * @return SecureStorage::Error::Errc::Success on success (even if data didn't exist),
     * or an error code if deletion fails.
     */
    Error::Errc deleteData(const std::string &data_id);

    /**
     * @brief Checks if a data item exists.
     *
     * @param data_id The unique identifier of the data item.
     * @return true if the data item (either main or backup file) exists, false otherwise.
     */
    bool dataExists(const std::string &data_id) const;

    /**
     * @brief Lists the IDs of all stored data items.
     * This is done by listing files with the .enc extension in the storage directory.
     *
     * @param[out] out_data_ids A vector to store the data IDs found.
     * @return SecureStorage::Error::Errc::Success on success, or an error code on failure.
     */
    Error::Errc listDataIds(std::vector<std::string> &out_data_ids) const;

private:
    std::string m_rootStoragePath;
    std::unique_ptr<Crypto::KeyProvider> m_keyProvider;
    std::unique_ptr<Crypto::Encryptor> m_encryptor;
    std::vector<unsigned char> m_masterKey; // Stores the derived master encryption key
    bool m_initialized;

    /**
     * @brief Constructs the full file path for a main data file.
     * @param data_id The data identifier.
     * @return The full path string.
     */
    std::string getDataFilePath(const std::string &data_id) const;

    /**
     * @brief Constructs the full file path for a backup data file.
     * @param data_id The data identifier.
     * @return The full path string.
     */
    std::string getBackupFilePath(const std::string &data_id) const;

    /**
     * @brief Constructs the full file path for a temporary data file.
     * @param data_id The data identifier.
     * @return The full path string.
     */
    std::string getTempFilePath(const std::string &data_id) const;

    /**
     * @brief Validates and sanitizes a data_id to ensure it's a safe filename component.
     * Checks for emptiness, path traversal characters ('/', '\'), '..', etc.
     *
     * @param data_id The data identifier to check.
     * @return SecureStorage::Error::Errc::Success if valid, Errc::InvalidArgument otherwise.
     */
    Error::Errc validateDataId(const std::string &data_id) const;
};

} // namespace Storage
} // namespace SecureStorage

#endif // SS_SECURE_STORE_H