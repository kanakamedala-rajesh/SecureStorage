#include "Logger.h"
#include <vector> // For a potential issue with MinGW put_time, include vector as a workaround if needed

namespace SecureStorage {
namespace Utils {

Logger::Logger() : m_currentLevel(LogLevel::INFO) { // Default log level
    // We could make the default log level configurable, e.g., via an environment variable
    // or a compile-time definition. For automotive, often INFO or WARNING is default.
    // For development, DEBUG might be preferred.
#ifdef NDEBUG // Release mode
    m_currentLevel = LogLevel::WARNING;
#else // Debug mode
    m_currentLevel = LogLevel::DEBUG;
#endif
}

Logger& Logger::getInstance() {
    static Logger instance; // Singleton instance
    return instance;
}

void Logger::setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentLevel = level;
}

std::string Logger::logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO ";
        case LogLevel::WARNING: return "WARN ";
        case LogLevel::ERROR:   return "ERROR";
        default:                return "UNKWN";
    }
}

std::string Logger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    // std::put_time might require #include <iomanip>
    // and some compilers/platforms (like older MinGW) might have issues with it.
    // A more portable way if std::put_time is problematic:
    std::tm buf;
#ifdef _WIN32
    localtime_s(&buf, &in_time_t);
#else
    localtime_r(&in_time_t, &buf); // POSIX thread-safe version
#endif

    std::ostringstream ss;
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    
    // Add milliseconds
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}


void Logger::log(LogLevel level, const std::string& message, const char* file, int line) {
    std::lock_guard<std::mutex> lock(m_mutex); // Ensure thread-safe output

    if (level < m_currentLevel) {
        return; // Skip logging if below current threshold
    }

    // Extracting filename from full path for brevity
    std::string filename_str(file);
    size_t last_slash = filename_str.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        filename_str = filename_str.substr(last_slash + 1);
    }

    std::cout << "[" << getCurrentTimestamp() << "] "
              << "[" << logLevelToString(level) << "] "
              << "[" << filename_str << ":" << line << "] "
              << message << std::endl;
}

} // namespace Utils
} // namespace SecureStorage