#ifndef SS_LOGGER_H
#define SS_LOGGER_H

#include <chrono>
#include <iomanip> // For std::put_time
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

// Forward declaration if needed, or include if simple enough
// #include "Error.h" // If logging error codes directly

namespace SecureStorage {
namespace Utils {

/**
 * @enum LogLevel
 * @brief Defines the severity levels for log messages.
 */
enum class LogLevel { DEBUG, INFO, WARNING, ERROR };

/**
 * @class Logger
 * @brief A simple thread-safe logger that outputs messages to std::cout.
 *
 * This logger provides basic logging capabilities with different levels.
 * It prepends timestamps and log levels to messages.
 * All logging operations are protected by a mutex for thread safety.
 */
class Logger {
public:
    /**
     * @brief Gets the singleton instance of the Logger.
     * @return Reference to the Logger instance.
     */
    static Logger &getInstance();

    /**
     * @brief Logs a message with a specified log level.
     * @param level The severity level of the message.
     * @param message The message string to log.
     * @param file The source file name where the log originated (often from __FILE__).
     * @param line The line number in the source file (often from __LINE__).
     */
    void log(LogLevel level, const std::string &message, const char *file, int line);

    /**
     * @brief Sets the minimum log level to output.
     * Messages below this level will be ignored.
     * @param level The minimum LogLevel.
     */
    void setLogLevel(LogLevel level);

private:
    Logger(); // Private constructor for singleton
    ~Logger() = default;
    Logger(const Logger &) = delete;            // Disable copy constructor
    Logger &operator=(const Logger &) = delete; // Disable copy assignment
    Logger(Logger &&) = delete;                 // Disable move constructor
    Logger &operator=(Logger &&) = delete;      // Disable move assignment

    static std::string logLevelToString(LogLevel level);
    static std::string getCurrentTimestamp();

    std::mutex m_mutex;      ///< Mutex to protect concurrent access to std::cout
    LogLevel m_currentLevel; ///< Current minimum log level to output
};

// Convenience macros for logging
// SS_LOG_X(message_stream)
#define SS_LOG(level, message)                                                                     \
    do {                                                                                           \
        std::ostringstream oss;                                                                    \
        oss << message;                                                                            \
        SecureStorage::Utils::Logger::getInstance().log(level, oss.str(), __FILE__, __LINE__);     \
    } while (false)

#define SS_LOG_DEBUG(message) SS_LOG(SecureStorage::Utils::LogLevel::DEBUG, message)
#define SS_LOG_INFO(message) SS_LOG(SecureStorage::Utils::LogLevel::INFO, message)
#define SS_LOG_WARN(message) SS_LOG(SecureStorage::Utils::LogLevel::WARNING, message)
#define SS_LOG_ERROR(message) SS_LOG(SecureStorage::Utils::LogLevel::ERROR, message)

// Example usage:
// SS_LOG_INFO("Application started with ID: " << appId);
// SS_LOG_ERROR("Failed to open file: " << filename << " - Error: " << error_code.message());

} // namespace Utils
} // namespace SecureStorage

#endif // SS_LOGGER_H