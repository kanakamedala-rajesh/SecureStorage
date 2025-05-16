// SecureStorage/tests/utils/test_Logger.cpp
#include "gtest/gtest.h"
#include "Logger.h" // Adjust path if necessary
#include <sstream> // For capturing cout

// Helper class to capture std::cout
class CoutRedirector {
public:
    CoutRedirector(std::streambuf* new_buffer)
        : old(std::cout.rdbuf(new_buffer)) {}

    ~CoutRedirector() {
        std::cout.rdbuf(old);
    }

private:
    std::streambuf* old;
};


TEST(LoggerTest, BasicLogging) {
    std::ostringstream oss;
    CoutRedirector redirect(oss.rdbuf()); // Redirect std::cout to our ostringstream

    SecureStorage::Utils::Logger::getInstance().setLogLevel(SecureStorage::Utils::LogLevel::DEBUG);
    SS_LOG_DEBUG("This is a debug message with value: " << 42);
    SS_LOG_INFO("This is an info message.");
    SS_LOG_WARN("This is a warning.");
    SS_LOG_ERROR("This is an error!");

    std::string output = oss.str();
    ASSERT_NE(output.find("DEBUG"), std::string::npos);
    ASSERT_NE(output.find("This is a debug message with value: 42"), std::string::npos);
    ASSERT_NE(output.find("INFO "), std::string::npos); // Note the space for alignment
    ASSERT_NE(output.find("This is an info message."), std::string::npos);
    ASSERT_NE(output.find("WARN "), std::string::npos); // Note the space for alignment
    ASSERT_NE(output.find("This is a warning."), std::string::npos);
    ASSERT_NE(output.find("ERROR"), std::string::npos);
    ASSERT_NE(output.find("This is an error!"), std::string::npos);
    ASSERT_NE(output.find("test_Logger.cpp"), std::string::npos); // Check if filename is present
}

TEST(LoggerTest, LogLevelFiltering) {
    std::ostringstream oss;
    CoutRedirector redirect(oss.rdbuf());

    SecureStorage::Utils::Logger::getInstance().setLogLevel(SecureStorage::Utils::LogLevel::WARNING);

    SS_LOG_DEBUG("This debug message should NOT appear.");
    SS_LOG_INFO("This info message should NOT appear.");
    SS_LOG_WARN("This warning SHOULD appear.");
    SS_LOG_ERROR("This error SHOULD appear.");

    std::string output = oss.str();
    ASSERT_EQ(output.find("DEBUG"), std::string::npos);
    ASSERT_EQ(output.find("INFO "), std::string::npos);
    ASSERT_NE(output.find("WARN "), std::string::npos);
    ASSERT_NE(output.find("This warning SHOULD appear."), std::string::npos);
    ASSERT_NE(output.find("ERROR"), std::string::npos);
    ASSERT_NE(output.find("This error SHOULD appear."), std::string::npos);

    // Reset to default for other tests potentially
    SecureStorage::Utils::Logger::getInstance().setLogLevel(SecureStorage::Utils::LogLevel::DEBUG);
}

TEST(LoggerTest, SingletonInstance) {
    auto& logger1 = SecureStorage::Utils::Logger::getInstance();
    auto& logger2 = SecureStorage::Utils::Logger::getInstance();
    ASSERT_EQ(&logger1, &logger2) << "Logger getInstance() should return the same instance.";
}