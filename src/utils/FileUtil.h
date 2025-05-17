#ifndef SS_FILE_UTIL_H
#define SS_FILE_UTIL_H

#include "Error.h" // For SecureStorage::Error::Errc
#include <string>
#include <vector>
#include <fstream> // For std::ifstream, std::ofstream

namespace SecureStorage {
namespace Utils {

/**
 * @class FileUtil
 * @brief Provides utility functions for file system operations.
 *
 * This class offers static methods for common file tasks such as reading,
 * writing (atomically), deleting, checking existence, and creating directories.
 * All operations are designed to integrate with the library's error reporting.
 */
class FileUtil {
public:
    FileUtil() = delete; // Static class, no instances

    /**
     * @brief Atomically writes data to a file.
     *
     * Writes to a temporary file first, then renames it to the final filepath.
     * This ensures that the original file (if it exists) is not corrupted
     * in case of an interruption (e.g., power loss) during the write.
     *
     * @param filepath The final path of the file to write.
     * @param data The byte vector containing data to write.
     * @return SecureStorage::Error::Errc::Success on success, or an error code on failure.
     */
    static Error::Errc atomicWriteFile(const std::string& filepath, const std::vector<unsigned char>& data);

    /**
     * @brief Reads the entire content of a file into a byte vector.
     *
     * @param filepath The path of the file to read.
     * @param[out] data The byte vector to store the file's content.
     * @return SecureStorage::Error::Errc::Success on success, or an error code on failure.
     */
    static Error::Errc readFile(const std::string& filepath, std::vector<unsigned char>& data);

    /**
     * @brief Deletes a file.
     *
     * @param filepath The path of the file to delete.
     * @return SecureStorage::Error::Errc::Success on success (or if file didn't exist),
     * or an error code if deletion fails for an existing file.
     */
    static Error::Errc deleteFile(const std::string& filepath);

    /**
     * @brief Checks if a file or directory exists at the given path.
     *
     * @param filepath The path to check.
     * @return true if the path exists, false otherwise.
     */
    static bool pathExists(const std::string& filepath);

    /**
     * @brief Creates all directories in the given path if they do not exist.
     *
     * Similar to `mkdir -p`.
     * @param path The directory path to create.
     * @return SecureStorage::Error::Errc::Success on success, or an error code on failure.
     */
    static Error::Errc createDirectories(const std::string& path);

    /**
     * @brief Extracts the directory part from a full file path.
     * @param filepath The full path to a file.
     * @return The directory path, or an empty string if not found.
     */
    static std::string getDirectory(const std::string& filepath);

    /**
     * @brief Lists all regular files in a given directory.
     * Does not recurse into subdirectories.
     * @param directoryPath The path to the directory.
     * @param[out] files A vector of strings to store the names of the files found.
     * @return SecureStorage::Error::Errc::Success on success, or an error code on failure.
     */
    static Error::Errc listDirectory(const std::string& directoryPath, std::vector<std::string>& files);

};

} // namespace Utils
} // namespace SecureStorage

#endif // SS_FILE_UTIL_H