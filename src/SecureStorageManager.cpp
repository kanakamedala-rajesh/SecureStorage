#include "SecureStorageManager.h" // Public API header
#include "storage/SecureStore.h" // Definition of SecureStore
#include "utils/Logger.h"        // For SS_LOG macros

namespace SecureStorage {

// PImpl (Pointer to Implementation) class
class SecureStorageManager::SecureStorageManagerImpl {
public:
    std::unique_ptr<Storage::SecureStore> secureStoreInstance;
    // std::unique_ptr<Watcher::FileWatcher> fileWatcherInstance; // Future addition
    bool isManagerInitialized;

    SecureStorageManagerImpl(const std::string& rootStoragePath, const std::string& deviceSerialNumber)
        : secureStoreInstance(nullptr),
          isManagerInitialized(false) {
        
        SS_LOG_INFO("SecureStorageManagerImpl: Initializing with root path: '" << rootStoragePath
                    << "' and device serial: '" << (deviceSerialNumber.empty() ? "EMPTY" : "PRESENT") << "'");

        // SecureStore constructor handles empty path/serial and logs, then isInitialized() will be false.
        // We create it directly with new for C++11 unique_ptr.
        secureStoreInstance = std::unique_ptr<Storage::SecureStore>(
            new Storage::SecureStore(rootStoragePath, deviceSerialNumber)
        );

        if (secureStoreInstance && secureStoreInstance->isInitialized()) {
            isManagerInitialized = true;
            SS_LOG_INFO("SecureStorageManagerImpl: SecureStore component initialized successfully.");
        } else {
            SS_LOG_ERROR("SecureStorageManagerImpl: SecureStore component failed to initialize.");
            // secureStoreInstance might be null if its constructor threw an unhandled exception,
            // or it's non-null but its internal isInitialized is false.
            // Resetting to nullptr if not properly initialized.
            secureStoreInstance.reset(); 
        }
    }

    ~SecureStorageManagerImpl() {
        SS_LOG_INFO("SecureStorageManagerImpl shutting down.");
        // unique_ptr will handle deletion of secureStoreInstance
    }
};


// --- SecureStorageManager Public API Implementation ---

SecureStorageManager::SecureStorageManager(const std::string& rootStoragePath, const std::string& deviceSerialNumber)
    : m_impl(new SecureStorageManagerImpl(rootStoragePath, deviceSerialNumber)) {}

SecureStorageManager::~SecureStorageManager() = default; // Needed for std::unique_ptr<PImpl>

// Move constructor
SecureStorageManager::SecureStorageManager(SecureStorageManager&& other) noexcept = default;

// Move assignment operator
SecureStorageManager& SecureStorageManager::operator=(SecureStorageManager&& other) noexcept = default;


bool SecureStorageManager::isInitialized() const {
    if (!m_impl) return false; // Should not happen if constructor ran
    return m_impl->isManagerInitialized;
}

Error::Errc SecureStorageManager::storeData(const std::string& data_id, const std::vector<unsigned char>& plain_data) {
    if (!isInitialized()) {
        SS_LOG_ERROR("SecureStorageManager::storeData called but manager is not initialized.");
        return Error::Errc::NotInitialized;
    }
    return m_impl->secureStoreInstance->storeData(data_id, plain_data);
}

Error::Errc SecureStorageManager::retrieveData(const std::string& data_id, std::vector<unsigned char>& out_plain_data) {
    if (!isInitialized()) {
        SS_LOG_ERROR("SecureStorageManager::retrieveData called but manager is not initialized.");
        out_plain_data.clear();
        return Error::Errc::NotInitialized;
    }
    return m_impl->secureStoreInstance->retrieveData(data_id, out_plain_data);
}

Error::Errc SecureStorageManager::deleteData(const std::string& data_id) {
    if (!isInitialized()) {
        SS_LOG_ERROR("SecureStorageManager::deleteData called but manager is not initialized.");
        return Error::Errc::NotInitialized;
    }
    return m_impl->secureStoreInstance->deleteData(data_id);
}

bool SecureStorageManager::dataExists(const std::string& data_id) const {
    if (!isInitialized()) {
        // SS_LOG_DEBUG("SecureStorageManager::dataExists called but manager is not initialized."); // Can be noisy
        return false;
    }
    return m_impl->secureStoreInstance->dataExists(data_id);
}

Error::Errc SecureStorageManager::listDataIds(std::vector<std::string>& out_data_ids) const {
    if (!isInitialized()) {
        SS_LOG_ERROR("SecureStorageManager::listDataIds called but manager is not initialized.");
        out_data_ids.clear();
        return Error::Errc::NotInitialized;
    }
    return m_impl->secureStoreInstance->listDataIds(out_data_ids);
}

} // namespace SecureStorage