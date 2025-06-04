#ifndef SYSTEM_ID_PROVIDER_H
#define SYSTEM_ID_PROVIDER_H

#include "ISystemIdProvider.h" // Base interface
#include <string>

namespace SecureStorage {
namespace Utils {

/**
 * @class SystemIdProvider
 * @brief Provides a system-specific identifier.
 *
 * This implementation attempts to retrieve a unique system identifier.
 * Currently, it tries to read /proc/sys/kernel/random/boot_id.
 * If unavailable, it falls back to a placeholder.
 *
 * TODO: Enhance this provider with more robust platform-specific methods
 * for fetching unique and stable hardware/OS identifiers.
 */
class SystemIdProvider : public ISystemIdProvider {
public:
    SystemIdProvider();
    ~SystemIdProvider() override = default;

    Error::Errc getSystemId(std::string& systemId) const override;

private:
    std::string m_cachedSystemId;
    bool m_isInitialized;

    Error::Errc initializeSystemId();
};

} // namespace Utils
} // namespace SecureStorage

#endif // SYSTEM_ID_PROVIDER_H
