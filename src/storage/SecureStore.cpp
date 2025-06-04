#include "SecureStore.h"
#include "utils/ISystemIdProvider.h" // ADDED
#include "utils/Logger.h" // For SS_LOG_ macros (Corrected include path for Logger if needed, assuming utils/Logger.h)
#include <algorithm> // For std::remove_if for data_id sanitization (not used yet)
#include <cerrno>    // For errno
#include <cstdio>    // For std::rename
#include <cstring>   // For strerror

namespace SecureStorage {
namespace Storage {

SecureStore::SecureStore(std::string rootStoragePath,
                         const Utils::ISystemIdProvider& systemIdProvider) // MODIFIED
    : m_rootStoragePath(std::move(rootStoragePath)),
      m_keyProvider(nullptr),
      m_encryptor(nullptr),
      m_initialized(false) {

    if (m_rootStoragePath.empty()) {
        SS_LOG_ERROR("SecureStore: Root storage path cannot be empty.");
        return; // m_initialized remains false
    }
    // REMOVED: deviceSerialNumber.empty() check, as systemIdProvider is now responsible

    // Ensure root path ends with a slash for consistent path joining
    if (m_rootStoragePath.back() != '/' && m_rootStoragePath.back() != '\\') {
        m_rootStoragePath += '/';
    }

    // Create root storage directory if it doesn't exist
    Error::Errc dirErr = Utils::FileUtil::createDirectories(m_rootStoragePath);
    if (dirErr != Error::Errc::Success) {
        SS_LOG_ERROR("SecureStore: Failed to create root storage directory: "
                     << m_rootStoragePath << " (Error: " << static_cast<int>(dirErr) << ")");
        return; // m_initialized remains false
    }

    // Initialize crypto components
    // Using C++11 style `new` for unique_ptr as make_unique is C++14
    m_keyProvider = std::unique_ptr<Crypto::KeyProvider>(
        new Crypto::KeyProvider(systemIdProvider)); // MODIFIED (passing the provider)
                                                    // Assuming default salt and info are handled by KeyProvider's constructor

    m_encryptor = std::unique_ptr<Crypto::Encryptor>(new Crypto::Encryptor()); // Uses default seed

    // Derive and store the master encryption key
    Error::Errc keyErr =
        m_keyProvider->getEncryptionKey(m_masterKey, Crypto::AES_GCM_KEY_SIZE_BYTES);
    if (keyErr != Error::Errc::Success) {
        SS_LOG_ERROR("SecureStore: Failed to derive master encryption key (Error: "
                     << Error::GetErrorMessage(keyErr) << ")"); // Use GetErrorMessage
        m_keyProvider.reset(); // Release resources if init fails partially
        m_encryptor.reset();
        return; // m_initialized remains false
    }

    SS_LOG_INFO("SecureStore initialized successfully. Root path: " << m_rootStoragePath);
    m_initialized = true;
}

bool SecureStore::isInitialized() const {
    return m_initialized;
}

std::string SecureStore::getDataFilePath(const std::string &data_id) const {
    return m_rootStoragePath + data_id + DATA_FILE_EXTENSION;
}

std::string SecureStore::getBackupFilePath(const std::string &data_id) const {
    // Backup file is just the main file path + .bak suffix to that full path
    return m_rootStoragePath + data_id + DATA_FILE_EXTENSION + BACKUP_FILE_EXTENSION;
}

std::string SecureStore::getTempFilePath(const std::string &data_id) const {
    return m_rootStoragePath + data_id + DATA_FILE_EXTENSION + TEMP_FILE_SUFFIX;
}

Error::Errc SecureStore::validateDataId(const std::string &data_id) const {
    if (data_id.empty()) {
        SS_LOG_WARN("Invalid data_id: cannot be empty.");
        return Error::Errc::InvalidArgument;
    }
    // Check for problematic characters like path separators or '..'
    // A more robust check might involve a whitelist of allowed characters.
    if (data_id.find('/') != std::string::npos || data_id.find('\\') != std::string::npos ||
        data_id.find("..") != std::string::npos) {
        SS_LOG_WARN("Invalid data_id: '" << data_id
                                         << "' contains forbidden characters or sequences.");
        return Error::Errc::InvalidArgument;
    }
    // Check length, etc. if needed
    return Error::Errc::Success;
}

Error::Errc SecureStore::storeData(const std::string &data_id,
                                   const std::vector<unsigned char> &plain_data) {
    if (!m_initialized) {
        SS_LOG_ERROR("SecureStore not initialized. Cannot store data.");
        return Error::Errc::NotInitialized;
    }
    Error::Errc id_validation_err = validateDataId(data_id);
    if (id_validation_err != Error::Errc::Success) {
        return id_validation_err;
    }

    std::vector<unsigned char> encrypted_data;
    Error::Errc enc_err = m_encryptor->encrypt(plain_data, m_masterKey, encrypted_data);
    if (enc_err != Error::Errc::Success) {
        SS_LOG_ERROR("Failed to encrypt data for id '"
                     << data_id << "'. Error: " << static_cast<int>(enc_err));
        return enc_err;
    }

    std::string main_file = getDataFilePath(data_id);
    std::string backup_file = getBackupFilePath(data_id);
    std::string temp_file = getTempFilePath(data_id); // Use a distinct temp file name

    // Step 1: Write encrypted data to a temporary file
    Error::Errc write_err = Utils::FileUtil::atomicWriteFile(temp_file, encrypted_data);
    if (write_err != Error::Errc::Success) {
        SS_LOG_ERROR("Failed to write encrypted data to temporary file '"
                     << temp_file << "' for id '" << data_id
                     << "'. Error: " << static_cast<int>(write_err));
        Utils::FileUtil::deleteFile(temp_file); // Attempt cleanup
        return write_err;
    }

    // Step 2: If main file exists, move it to backup
    // We must delete any old backup first to allow rename to succeed if backup_file exists.
    if (Utils::FileUtil::pathExists(main_file)) {
        if (Utils::FileUtil::pathExists(backup_file)) {
            Error::Errc del_bak_err = Utils::FileUtil::deleteFile(backup_file);
            if (del_bak_err != Error::Errc::Success) {
                SS_LOG_WARN("Failed to delete old backup file '"
                            << backup_file << "'. Proceeding, but old backup might persist. Error: "
                            << static_cast<int>(del_bak_err));
                // Potentially critical, decide if we should abort. For now, continue.
            }
        }
        if (std::rename(main_file.c_str(), backup_file.c_str()) != 0) {
            SS_LOG_WARN("Failed to move main file '" << main_file << "' to backup '" << backup_file
                                                     << "'. Error: " << strerror(errno)
                                                     << ". Proceeding to write main file.");
            // If this rename fails, the old main_file is still there.
            // The new data will overwrite it in the next step if std::rename for temp->main works.
            // This is not ideal for the backup strategy but makes the write more likely to succeed.
            // A more robust approach might involve more stages or error out here.
        } else {
            SS_LOG_DEBUG("Moved existing main file '" << main_file << "' to backup '" << backup_file
                                                      << "'.");
        }
    }

    // Step 3: Move temporary file to main file
    if (std::rename(temp_file.c_str(), main_file.c_str()) != 0) {
        SS_LOG_ERROR("CRITICAL: Failed to rename temp file '"
                     << temp_file << "' to main file '" << main_file << "'. Error: "
                     << strerror(errno) << ". Data might be in temp file or backup.");
        // Attempt to restore backup if it exists and main failed to be created
        if (Utils::FileUtil::pathExists(backup_file) && !Utils::FileUtil::pathExists(main_file)) {
            SS_LOG_INFO("Attempting to restore backup '" << backup_file << "' to main '"
                                                         << main_file
                                                         << "' due to final rename failure.");
            if (std::rename(backup_file.c_str(), main_file.c_str()) == 0) {
                SS_LOG_INFO(
                    "Successfully restored backup to main file after temp->main rename failure.");
            } else {
                SS_LOG_ERROR("Failed to restore backup to main file. Data for '"
                             << data_id << "' may be inconsistent.");
            }
        }
        Utils::FileUtil::deleteFile(temp_file); // Clean up temp file in any case
        return Error::Errc::FileRenameFailed;   // Indicate a significant failure
    }

    SS_LOG_INFO("Successfully stored data for id '" << data_id << "' to '" << main_file << "'.");
    return Error::Errc::Success;
}

Error::Errc SecureStore::retrieveData(const std::string &data_id,
                                      std::vector<unsigned char> &out_plain_data) {
    out_plain_data.clear();
    if (!m_initialized) {
        SS_LOG_ERROR("SecureStore not initialized. Cannot retrieve data.");
        return Error::Errc::NotInitialized;
    }
    Error::Errc id_validation_err = validateDataId(data_id);
    if (id_validation_err != Error::Errc::Success) {
        return id_validation_err;
    }

    std::string main_file = getDataFilePath(data_id);
    std::string backup_file = getBackupFilePath(data_id);
    std::vector<unsigned char> encrypted_data_to_decrypt; // Will hold data from main or backup
    bool retrieved_from_main = false;
    bool retrieved_from_backup = false;

    // --- Stage 1: Try Main File ---
    SS_LOG_DEBUG("Attempting to retrieve data for id '" << data_id
                                                        << "' from main file: " << main_file);
    Error::Errc main_read_err = Utils::FileUtil::readFile(main_file, encrypted_data_to_decrypt);

    if (main_read_err == Error::Errc::Success) {
        Error::Errc main_dec_err =
            m_encryptor->decrypt(encrypted_data_to_decrypt, m_masterKey, out_plain_data);
        if (main_dec_err == Error::Errc::Success) {
            SS_LOG_INFO("Successfully retrieved and decrypted data for id '"
                        << data_id << "' from main file.");
            retrieved_from_main = true;
            // Optionally, if we know a backup exists and might be older, consider deleting it
            // For now, successful retrieval from main is enough.
        } else {
            SS_LOG_WARN(
                "Failed to decrypt main data file '"
                << main_file << "' for id '" << data_id << "'. Error: "
                << Error::GetErrorMessage(main_dec_err) // Using GetErrorMessage
                << " (" << static_cast<int>(main_dec_err) << "). Will attempt backup.");
            // Consider deleting the corrupted main file to prevent reuse,
            // especially if backup retrieval is successful.
            // Utils::FileUtil::deleteFile(main_file); // Or do this after successful backup
            // retrieval & restore
        }
    } else {
        SS_LOG_WARN(
            "Failed to read main data file '"
            << main_file << "' for id '" << data_id << "'. Error: "
            << Error::GetErrorMessage(main_read_err) // Using GetErrorMessage
            << " (" << static_cast<int>(main_read_err) << "). Will attempt backup.");
    }

    if (retrieved_from_main) {
        return Error::Errc::Success;
    }

    // --- Stage 2: Try Backup File (if main file attempt failed) ---
    SS_LOG_INFO("Attempting to retrieve data for id '" << data_id
                                                       << "' from backup file: " << backup_file);
    // Clear buffer in case main file read partially filled it but then decryption failed
    encrypted_data_to_decrypt.clear();
    out_plain_data.clear(); // Clear output from any failed main attempt

    Error::Errc backup_read_err = Utils::FileUtil::readFile(backup_file, encrypted_data_to_decrypt);
    if (backup_read_err != Error::Errc::Success) {
        SS_LOG_ERROR(
            "Failed to read backup data file '"
            << backup_file << "' for id '" << data_id << "'. Error: "
            << Error::GetErrorMessage(backup_read_err) // Using GetErrorMessage
            << " (" << static_cast<int>(backup_read_err) << "). Data not found.");
        return Error::Errc::DataNotFound; // Main failed (read or decrypt), and backup read failed.
    }

    Error::Errc backup_dec_err =
        m_encryptor->decrypt(encrypted_data_to_decrypt, m_masterKey, out_plain_data);
    if (backup_dec_err != Error::Errc::Success) {
        SS_LOG_ERROR(
            "Failed to decrypt backup data file '"
            << backup_file << "' for id '" << data_id << "'. Error: "
            << Error::GetErrorMessage(backup_dec_err) // Using GetErrorMessage
            << " (" << static_cast<int>(backup_dec_err) << "). Data recovery failed.");
        return backup_dec_err; // Main failed, backup decryption failed.
    }

    // If we are here, data was successfully read and decrypted from backup
    retrieved_from_backup = true;
    SS_LOG_INFO("Data for id '"
                << data_id
                << "' was successfully retrieved from backup. Attempting to restore to main file.");

    // Before restoring, if the main file failed due to corruption (not just missing), delete it.
    if (main_read_err == Error::Errc::Success) { // Implies main file existed but failed decryption
        SS_LOG_DEBUG("Deleting potentially corrupted main file '"
                     << main_file << "' before restoring from backup.");
        Utils::FileUtil::deleteFile(main_file);
    }

    Error::Errc write_main_err = Utils::FileUtil::atomicWriteFile(
        main_file, encrypted_data_to_decrypt); // Write the raw ENCRYPTED backup data
    if (write_main_err == Error::Errc::Success) {
        SS_LOG_INFO("Successfully restored backup data to main file: " << main_file);
    } else {
        SS_LOG_WARN(
            "Failed to restore backup data to main file '"
            << main_file << "'. Error: "
            << Error::GetErrorMessage(write_main_err) // Using GetErrorMessage
            << " (" << static_cast<int>(write_main_err)
            << "). Main file may be missing or outdated for next read.");
        // Data is still successfully retrieved for this call, this is just a warning about the
        // restore op.
    }
    return Error::Errc::Success;
}

Error::Errc SecureStore::deleteData(const std::string &data_id) {
    if (!m_initialized) {
        SS_LOG_ERROR("SecureStore not initialized. Cannot delete data.");
        return Error::Errc::NotInitialized;
    }
    Error::Errc id_validation_err = validateDataId(data_id);
    if (id_validation_err != Error::Errc::Success) {
        return id_validation_err; // Don't proceed with invalid ID
    }

    std::string main_file = getDataFilePath(data_id);
    std::string backup_file = getBackupFilePath(data_id);
    bool main_existed = Utils::FileUtil::pathExists(main_file);
    bool backup_existed = Utils::FileUtil::pathExists(backup_file);

    Error::Errc del_main_err = Utils::FileUtil::deleteFile(main_file);
    Error::Errc del_bak_err = Utils::FileUtil::deleteFile(backup_file);

    if (del_main_err != Error::Errc::Success &&
        main_existed) { // Only error if it existed and failed to delete
        SS_LOG_ERROR("Failed to delete main data file '"
                     << main_file << "'. Error: " << static_cast<int>(del_main_err));
        // If main delete failed, backup delete result is still relevant but the operation overall
        // failed.
        return del_main_err;
    }
    if (del_bak_err != Error::Errc::Success &&
        backup_existed) { // Only error if it existed and failed to delete
        SS_LOG_ERROR("Failed to delete backup data file '"
                     << backup_file << "'. Error: " << static_cast<int>(del_bak_err));
        // Main might have been deleted successfully, but backup failed.
        return del_bak_err;
    }

    SS_LOG_INFO("Successfully deleted data (if existed) for id '" << data_id << "'.");
    return Error::Errc::Success;
}

bool SecureStore::dataExists(const std::string &data_id) const {
    if (!m_initialized)
        return false;
    if (validateDataId(data_id) != Error::Errc::Success)
        return false;

    return Utils::FileUtil::pathExists(getDataFilePath(data_id)) ||
           Utils::FileUtil::pathExists(getBackupFilePath(data_id));
}

Error::Errc SecureStore::listDataIds(std::vector<std::string> &out_data_ids) const {
    out_data_ids.clear();
    if (!m_initialized) {
        SS_LOG_ERROR("SecureStore not initialized. Cannot list data IDs.");
        return Error::Errc::NotInitialized;
    }

    std::vector<std::string> all_files;
    Error::Errc list_err = Utils::FileUtil::listDirectory(m_rootStoragePath, all_files);
    if (list_err != Error::Errc::Success) {
        SS_LOG_ERROR("Failed to list directory '" << m_rootStoragePath
                                                  << "'. Error: " << static_cast<int>(list_err));
        return list_err;
    }

    for (const auto &filename : all_files) {
        // Check if filename ends with DATA_FILE_EXTENSION but not BACKUP_FILE_EXTENSION
        if (filename.length() > DATA_FILE_EXTENSION.length() &&
            filename.substr(filename.length() - DATA_FILE_EXTENSION.length()) ==
                DATA_FILE_EXTENSION) {
            // This is a .enc file. Extract data_id.
            std::string data_id =
                filename.substr(0, filename.length() - DATA_FILE_EXTENSION.length());
            // Ensure it's not a .bak file's .enc part by checking if it also ends with .bak's full
            // structure
            if (filename.length() >
                    (DATA_FILE_EXTENSION.length() + BACKUP_FILE_EXTENSION.length()) &&
                filename.substr(filename.length() -
                                (DATA_FILE_EXTENSION.length() + BACKUP_FILE_EXTENSION.length())) ==
                    (DATA_FILE_EXTENSION + BACKUP_FILE_EXTENSION)) {
                // This is actually a backup file like id.enc.bak, skip it as a primary data_id
                // source
                continue;
            }
            if (validateDataId(data_id) == Error::Errc::Success) { // Quick check on parsed ID
                out_data_ids.push_back(data_id);
            } else {
                SS_LOG_WARN("Found file '"
                            << filename
                            << "' in storage that does not map to a valid data_id, skipping.");
            }
        }
    }
    std::sort(out_data_ids.begin(), out_data_ids.end()); // Consistent order
    SS_LOG_DEBUG("Found " << out_data_ids.size() << " data IDs in storage path.");
    return Error::Errc::Success;
}

} // namespace Storage
} // namespace SecureStorage