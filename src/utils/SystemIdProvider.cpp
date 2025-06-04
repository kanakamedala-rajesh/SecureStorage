#include "SystemIdProvider.h"
#include "FileUtil.h" // For reading files like boot_id
#include "Logger.h"   // For logging
#include <fstream>    // For std::ifstream

// TODO: Define these paths in a more configurable way if necessary
#define BOOT_ID_PATH "/proc/sys/kernel/random/boot_id"
#define PLACEHOLDER_ID "default_system_id_placeholder_v1"

namespace SecureStorage {
namespace Utils {

SystemIdProvider::SystemIdProvider() : m_isInitialized(false) {
    // Initialization is deferred to the first call to getSystemId
    // or can be explicitly called if needed earlier.
    // For simplicity here, we'll let getSystemId handle it.
}

Error::Errc SystemIdProvider::initializeSystemId() {
    if (m_isInitialized) {
        return Error::Errc::Success;
    }

    SS_LOG_DEBUG("Initializing SystemIdProvider...");

    std::string bootId;
    Error::Errc readResult = FileUtil::readFileToString(BOOT_ID_PATH, bootId);

    if (readResult == Error::Errc::Success && !bootId.empty()) {
        // Remove newline characters if any, as boot_id might have one
        if (!bootId.empty() && bootId.back() == '\n') {
            bootId.pop_back();
        }
        m_cachedSystemId = bootId;
        SS_LOG_INFO("Successfully read system boot_id: " << m_cachedSystemId);
    } else {
        SS_LOG_WARN("Could not read system boot_id from " BOOT_ID_PATH ". Error: " << Error::GetErrorMessage(readResult) << ". Falling back to placeholder ID.");
        // TODO: Implement a more robust fallback, e.g., generate UUID and persist.
        // For now, using a fixed placeholder. This is NOT secure for production.
        m_cachedSystemId = PLACEHOLDER_ID;
        // This isn't an error for initialization itself, but a degraded state.
    }

    if (m_cachedSystemId.empty()) {
         SS_LOG_ERROR("System ID is empty after initialization attempts.");
         return Error::Errc::SystemError; // Should not happen with placeholder
    }

    m_isInitialized = true;
    return Error::Errc::Success;
}

Error::Errc SystemIdProvider::getSystemId(std::string& systemId) const {
    // Const_cast is necessary here because initializeSystemId modifies member variables.
    // This is a common pattern for lazy initialization in a const method.
    // Ensure that initializeSystemId() itself is thread-safe if this class
    // is used concurrently without external locking. For now, assuming single-threaded access
    // or external synchronization for initialization.
    SystemIdProvider* self = const_cast<SystemIdProvider*>(this);
    Error::Errc initErr = self->initializeSystemId();
    if (initErr != Error::Errc::Success) {
        return initErr;
    }

    if (!m_isInitialized || m_cachedSystemId.empty()) {
        SS_LOG_ERROR("SystemIdProvider not properly initialized or ID is empty.");
        return Error::Errc::NotInitialized;
    }
    systemId = m_cachedSystemId;
    return Error::Errc::Success;
}

} // namespace Utils
} // namespace SecureStorage
