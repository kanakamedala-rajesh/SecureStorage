#include "SecureStorageManager.h"     // Public API header
#include "file_watcher/FileWatcher.h" // FileWatcher definition
#include "storage/SecureStore.h"      // Definition of SecureStore
#include "utils/Logger.h"             // For SS_LOG macros

namespace SecureStorage {

// PImpl (Pointer to Implementation) class
class SecureStorageManager::SecureStorageManagerImpl {
public:
    std::unique_ptr<Storage::SecureStore> secureStoreInstance;
    std::unique_ptr<FileWatcher::FileWatcher> fileWatcherInstance; // Future addition
    bool isManagerInitialized;
    bool isFileWatcherActive;

    // Constructor initializes the SecureStore and integrates FileWatcher
    SecureStorageManagerImpl(const std::string &rootStoragePath,
                             const std::string &deviceSerialNumber,
                             FileWatcher::EventCallback fileWatcherCallback =
                                 nullptr // Optional callback for file watcher events
                             )
        : secureStoreInstance(nullptr), fileWatcherInstance(nullptr), isManagerInitialized(false),
          isFileWatcherActive(false) {

        SS_LOG_INFO("SecureStorageManagerImpl: Initializing with root path: '"
                    << rootStoragePath << "' and device serial: '"
                    << (deviceSerialNumber.empty() ? "EMPTY" : "PRESENT") << "'");

        secureStoreInstance = std::unique_ptr<Storage::SecureStore>(
            new Storage::SecureStore(rootStoragePath, deviceSerialNumber));

        if (secureStoreInstance && secureStoreInstance->isInitialized()) {
            isManagerInitialized = true;
            SS_LOG_INFO(
                "SecureStorageManagerImpl: SecureStore component initialized successfully.");

            // Initialize and start the FileWatcher
            fileWatcherInstance = std::unique_ptr<FileWatcher::FileWatcher>(
                new FileWatcher::FileWatcher(fileWatcherCallback));

            if (fileWatcherInstance) {
                if (fileWatcherInstance->start()) {
                    SS_LOG_DEBUG("SecureStorageManagerImpl: FileWatcher core started, attempting "
                                 "to add watch.");
                    if (fileWatcherInstance->addWatch(rootStoragePath)) {
                        isFileWatcherActive = true;
                        SS_LOG_INFO(
                            "SecureStorageManagerImpl: FileWatcher started and watching path: "
                            << rootStoragePath);
                    } else {
                        SS_LOG_ERROR("SecureStorageManagerImpl: Failed to add watch to FileWatcher "
                                     "for path: "
                                     << rootStoragePath << ". Stopping watcher.");
                        fileWatcherInstance->stop(); // Stop it if watch couldn't be added
                        fileWatcherInstance.reset();
                    }
                } else {
                    SS_LOG_ERROR("SecureStorageManagerImpl: Failed to start FileWatcher core.");
                    fileWatcherInstance.reset();
                }
            } else {
                SS_LOG_ERROR("SecureStorageManagerImpl: Failed to create FileWatcher instance.");
            }

        } else {
            SS_LOG_ERROR("SecureStorageManagerImpl: SecureStore component failed to initialize. "
                         "File watcher will not be started.");
            secureStoreInstance.reset();
        }
    }

    ~SecureStorageManagerImpl() {
        SS_LOG_INFO("SecureStorageManagerImpl shutting down...");
        if (fileWatcherInstance) {
            SS_LOG_DEBUG("SecureStorageManagerImpl: Stopping FileWatcher...");
            fileWatcherInstance->stop();
            fileWatcherInstance.reset(); // Explicitly reset after stopping
            SS_LOG_DEBUG("SecureStorageManagerImpl: FileWatcher stopped and reset.");
        }
        // unique_ptr will handle deletion of secureStoreInstance if not already null
        if (secureStoreInstance) {
            secureStoreInstance.reset();
            SS_LOG_DEBUG("SecureStorageManagerImpl: SecureStore reset.");
        }
    }
};

// --- SecureStorageManager Public API Implementation ---

SecureStorageManager::SecureStorageManager(const std::string &rootStoragePath,
                                           const std::string &deviceSerialNumber,
                                           FileWatcher::EventCallback fileWatcherCallback = nullptr)
    : m_impl(
          new SecureStorageManagerImpl(rootStoragePath, deviceSerialNumber, fileWatcherCallback)) {
}

SecureStorageManager::~SecureStorageManager() = default; // Needed for std::unique_ptr<PImpl>

// Move constructor
SecureStorageManager::SecureStorageManager(SecureStorageManager &&other) noexcept = default;

// Move assignment operator
SecureStorageManager &
SecureStorageManager::operator=(SecureStorageManager &&other) noexcept = default;

bool SecureStorageManager::isInitialized() const {
    if (!m_impl)
        return false;
    // Manager is considered initialized if the core SecureStore is initialized.
    // Watcher status is secondary for the overall manager readiness for storage operations.
    return m_impl->isManagerInitialized;
}

bool SecureStorageManager::isFileWatcherActive() const {
    if (!m_impl)
        return false;
    return m_impl->isFileWatcherActive;
}

Error::Errc SecureStorageManager::storeData(const std::string &data_id,
                                            const std::vector<unsigned char> &plain_data) {
    if (!isInitialized()) {
        SS_LOG_ERROR("SecureStorageManager::storeData called but manager is not initialized.");
        return Error::Errc::NotInitialized;
    }
    return m_impl->secureStoreInstance->storeData(data_id, plain_data);
}

Error::Errc SecureStorageManager::retrieveData(const std::string &data_id,
                                               std::vector<unsigned char> &out_plain_data) {
    if (!isInitialized()) {
        SS_LOG_ERROR("SecureStorageManager::retrieveData called but manager is not initialized.");
        out_plain_data.clear();
        return Error::Errc::NotInitialized;
    }
    return m_impl->secureStoreInstance->retrieveData(data_id, out_plain_data);
}

Error::Errc SecureStorageManager::deleteData(const std::string &data_id) {
    if (!isInitialized()) {
        SS_LOG_ERROR("SecureStorageManager::deleteData called but manager is not initialized.");
        return Error::Errc::NotInitialized;
    }
    return m_impl->secureStoreInstance->deleteData(data_id);
}

bool SecureStorageManager::dataExists(const std::string &data_id) const {
    if (!isInitialized()) {
        // SS_LOG_DEBUG("SecureStorageManager::dataExists called but manager is not initialized.");
        // // Can be noisy
        return false;
    }
    return m_impl->secureStoreInstance->dataExists(data_id);
}

Error::Errc SecureStorageManager::listDataIds(std::vector<std::string> &out_data_ids) const {
    if (!isInitialized()) {
        SS_LOG_ERROR("SecureStorageManager::listDataIds called but manager is not initialized.");
        out_data_ids.clear();
        return Error::Errc::NotInitialized;
    }
    return m_impl->secureStoreInstance->listDataIds(out_data_ids);
}

} // namespace SecureStorage