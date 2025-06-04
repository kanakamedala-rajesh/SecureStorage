#ifndef I_SYSTEM_ID_PROVIDER_H
#define I_SYSTEM_ID_PROVIDER_H

#include <string>
#include "Error.h" // For SecureStorage::Error::Errc

namespace SecureStorage {
namespace Utils {

/**
 * @interface ISystemIdProvider
 * @brief Interface for components that provide a system-specific identifier.
 *
 * This interface abstracts the mechanism of obtaining a unique or semi-unique
 * identifier for the system, which can be used as part of the key derivation
 * process to enhance security beyond just a device serial number.
 */
class ISystemIdProvider {
public:
    virtual ~ISystemIdProvider() = default;

    /**
     * @brief Gets the system-specific identifier.
     *
     * @param[out] systemId The string to store the retrieved system ID.
     * @return SecureStorage::Error::Errc::Success on success, or an error code on failure.
     *         Possible errors include:
     *         - Errc::SystemResourceNotFound if a required system resource (e.g., file) is not found.
     *         - Errc::SystemError if there's a general error reading system information.
     *         - Errc::NotImplemented if the method is not implemented by a concrete class.
     */
    virtual Error::Errc getSystemId(std::string& systemId) const = 0;
};

} // namespace Utils
} // namespace SecureStorage

#endif // I_SYSTEM_ID_PROVIDER_H
