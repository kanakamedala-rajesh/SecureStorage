#include "FileUtil.h"
#include "Logger.h" // For SS_LOG_ macros

#include <cstdio>   // For std::remove, std::rename
#include <sys/stat.h> // For mkdir, stat
#include <cerrno>   // For errno
#include <cstring>  // For strerror
#include <unistd.h> // For fsync (POSIX)

// For directory listing (POSIX)
#include <dirent.h>
#include <sys/types.h>

namespace SecureStorage {
namespace Utils {

std::string FileUtil::getDirectory(const std::string& filepath) {
    size_t last_slash_idx = filepath.find_last_of("/\\");
    if (std::string::npos != last_slash_idx) {
        return filepath.substr(0, last_slash_idx);
    }
    return ""; // Or "." for current directory if appropriate
}

Error::Errc FileUtil::atomicWriteFile(const std::string& filepath, const std::vector<unsigned char>& data) {
    if (filepath.empty()) {
        SS_LOG_ERROR("Filepath for atomic write is empty.");
        return Error::Errc::InvalidArgument;
    }

    std::string tempFilepath = filepath + ".tmp";
    std::string backupFilepath = filepath + ".bak"; // For a more robust 2-stage atomic, not fully implemented here yet for simplicity

    // Step 1: Write to temporary file
    { // Scope for ofstream
        std::ofstream ofs(tempFilepath, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open() || !ofs.good()) {
            SS_LOG_ERROR("Failed to open temporary file for writing: " << tempFilepath << " - " << strerror(errno));
            return Error::Errc::FileOpenFailed;
        }

        if (!data.empty()) {
            ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
            if (!ofs.good()) {
                SS_LOG_ERROR("Failed to write data to temporary file: " << tempFilepath << " - " << strerror(errno));
                ofs.close();
                std::remove(tempFilepath.c_str()); // Clean up temp file
                return Error::Errc::FileWriteFailed;
            }
        }
        ofs.flush(); // Ensure data is written from buffer to OS

        // fsync to ensure data is physically written to disk (for POSIX)
        // This requires a file descriptor.
        // For fstream, there's no standard C++ way to get the FD.
        // We can close and reopen with C-style IO, or rely on OS behavior after close.
        // For critical systems, getting the FD and calling fsync is better.
        // As a simpler C++11 approach, flush and close should be generally okay for now.
        // If true fsync is needed, platform-specific code or C-style I/O is required.
        // For now, relying on close() to flush.
        ofs.close();
        if (ofs.fail() && !ofs.eof()) { // Check state after close
             SS_LOG_ERROR("Error during close after writing temporary file: " << tempFilepath << " - " << strerror(errno));
             std::remove(tempFilepath.c_str());
             return Error::Errc::FileWriteFailed;
        }
    }
    SS_LOG_DEBUG("Successfully wrote data to temporary file: " << tempFilepath);

    // Step 2: Rename temporary file to the final filepath.
    // This is generally an atomic operation on POSIX compliant filesystems
    // if both paths are on the same filesystem.
    if (std::rename(tempFilepath.c_str(), filepath.c_str()) != 0) {
        SS_LOG_ERROR("Failed to rename temporary file '" << tempFilepath << "' to '" << filepath << "' - " << strerror(errno));
        std::remove(tempFilepath.c_str()); // Clean up temp file
        return Error::Errc::FileRenameFailed;
    }

    SS_LOG_DEBUG("Successfully renamed temp file to: " << filepath);

    // Optional: fsync the directory to ensure the rename operation is persistent (POSIX)
    // This is more advanced and requires opening the directory and calling fsync on its FD.
    // std::string dirPath = getDirectory(filepath);
    // if (!dirPath.empty()) {
    //     int dir_fd = open(dirPath.c_str(), O_RDONLY | O_DIRECTORY); // POSIX
    //     if (dir_fd >= 0) {
    //         fsync(dir_fd);
    //         close(dir_fd);
    //     }
    // }

    return Error::Errc::Success;
}

Error::Errc FileUtil::readFile(const std::string& filepath, std::vector<unsigned char>& data) {
    data.clear();
    if (filepath.empty()) {
        SS_LOG_ERROR("Filepath for read is empty.");
        return Error::Errc::InvalidArgument;
    }

    std::ifstream ifs(filepath, std::ios::binary | std::ios::ate); // ate: open at end to get size
    if (!ifs.is_open() || !ifs.good()) {
        // Log as debug, as file not existing is often a normal condition checked by pathExists first
        SS_LOG_DEBUG("Failed to open file for reading: " << filepath << " - " << strerror(errno));
        return Error::Errc::FileOpenFailed;
    }

    std::streamsize size = ifs.tellg();
    if (size < 0) { // Error getting size
        SS_LOG_ERROR("Failed to determine size of file: " << filepath);
        return Error::Errc::FileReadFailed;
    }
    ifs.seekg(0, std::ios::beg); // Go back to the beginning

    if (size == 0) { // Empty file
        SS_LOG_DEBUG("File is empty: " << filepath);
        return Error::Errc::Success; // Successfully read an empty file
    }

    data.resize(static_cast<size_t>(size));
    if (!data.empty()) {
         ifs.read(reinterpret_cast<char*>(data.data()), size);
         if (!ifs.good() && !ifs.eof()) { // eof is okay if we read exactly to the end
            SS_LOG_ERROR("Failed to read data from file: " << filepath << " - Read " << ifs.gcount() << " of " << size << " bytes. Error: " << strerror(errno));
            data.clear();
            return Error::Errc::FileReadFailed;
        }
    }
    SS_LOG_DEBUG("Successfully read " << data.size() << " bytes from file: " << filepath);
    return Error::Errc::Success;
}

Error::Errc FileUtil::deleteFile(const std::string& filepath) {
    if (filepath.empty()) {
        SS_LOG_ERROR("Filepath for delete is empty.");
        return Error::Errc::InvalidArgument;
    }
    if (!pathExists(filepath)) {
        SS_LOG_DEBUG("File to delete does not exist, no action needed: " << filepath);
        return Error::Errc::Success; // Not an error if it's already gone
    }

    if (std::remove(filepath.c_str()) != 0) {
        SS_LOG_ERROR("Failed to delete file: " << filepath << " - " << strerror(errno));
        return Error::Errc::FileRemoveFailed;
    }
    SS_LOG_DEBUG("Successfully deleted file: " << filepath);
    return Error::Errc::Success;
}

bool FileUtil::pathExists(const std::string& filepath) {
    if (filepath.empty()) {
        return false;
    }
    struct stat buffer;
    return (stat(filepath.c_str(), &buffer) == 0);
}

Error::Errc FileUtil::createDirectories(const std::string& path) {
    if (path.empty()) {
        SS_LOG_ERROR("Path for createDirectories is empty.");
        return Error::Errc::InvalidArgument;
    }

    // If path already exists and is a directory, success.
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            SS_LOG_DEBUG("Path already exists and is a directory: " << path);
            return Error::Errc::Success;
        } else {
            SS_LOG_ERROR("Path exists but is not a directory: " << path);
            return Error::Errc::OperationFailed; // Or a more specific error
        }
    }

    // Path doesn't exist, try to create it.
    // Recursive creation (mkdir -p behavior)
    std::string currentPath;
    size_t start = 0;
    size_t end = 0;

    // Handle absolute paths starting with '/'
    if (!path.empty() && path[0] == '/') {
        currentPath = "/";
        start = 1; // Start searching for next '/' after the initial one
    }

    while ((end = path.find_first_of('/', start)) != std::string::npos) {
        currentPath += path.substr(start, end - start + 1);
        start = end + 1;
        // Skip empty segments like in "//" or trailing "/" if path isn't just "/"
        if (currentPath.length() > 0 && (currentPath.length() > 1 || currentPath[0] != '/')) {
             // Check if this part of the path exists, if not create it
            if (!pathExists(currentPath)) {
                #ifdef _WIN32
                    // _mkdir is for Windows, but we assume POSIX for automotive Linux / Android NDK
                    // if (_mkdir(currentPath.c_str()) != 0 && errno != EEXIST) {
                #else
                    if (mkdir(currentPath.c_str(), 0755) != 0 && errno != EEXIST) { // 0755 permissions
                #endif
                        SS_LOG_ERROR("Failed to create directory: " << currentPath << " - " << strerror(errno));
                        return Error::Errc::OperationFailed;
                    }
                    SS_LOG_DEBUG("Created directory component: " << currentPath);
            }
        }
    }

    // Create the final component if it wasn't a trailing slash
    if (start < path.length()) {
        currentPath += path.substr(start);
    }
    // Ensure full path is handled if it doesn't end with '/'
    if (!currentPath.empty() && !pathExists(currentPath)) {
        #ifdef _WIN32
            // if (_mkdir(currentPath.c_str()) != 0 && errno != EEXIST) {
        #else
            if (mkdir(currentPath.c_str(), 0755) != 0 && errno != EEXIST) {
        #endif
            SS_LOG_ERROR("Failed to create final directory: " << currentPath << " - " << strerror(errno));
            return Error::Errc::OperationFailed;
        }
        SS_LOG_DEBUG("Created final directory component: " << currentPath);
    } else if (pathExists(currentPath) && S_ISDIR( (stat(currentPath.c_str(), &st) == 0 ? st.st_mode : 0) ) ) {
         // Final component already exists as a directory
    } else if (!pathExists(currentPath) && currentPath == path) {
        // Case where path is simple like "mydir" and doesn't exist
        #ifdef _WIN32
        #else
            if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
                SS_LOG_ERROR("Failed to create directory: " << path << " - " << strerror(errno));
                return Error::Errc::OperationFailed;
            }
            SS_LOG_DEBUG("Created directory: " << path);
        #endif
    }


    // Final check
    if (!pathExists(path)) {
         SS_LOG_ERROR("Directory creation failed or path does not exist after attempt: " << path);
         return Error::Errc::OperationFailed;
    }
    if (stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) {
        SS_LOG_ERROR("Path exists but is not a directory after creation attempt: " << path);
        return Error::Errc::OperationFailed;
    }

    SS_LOG_DEBUG("Successfully ensured directory exists: " << path);
    return Error::Errc::Success;
}

Error::Errc FileUtil::listDirectory(const std::string& directoryPath, std::vector<std::string>& files) {
    files.clear();
    if (directoryPath.empty()) {
        SS_LOG_ERROR("Directory path for listing is empty.");
        return Error::Errc::InvalidArgument;
    }

    DIR* dir = opendir(directoryPath.c_str());
    if (dir == nullptr) {
        SS_LOG_ERROR("Failed to open directory: " << directoryPath << " - " << strerror(errno));
        return Error::Errc::FileOpenFailed; // Or PathNotFound
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }

        // Check if it's a regular file (optional, could also list directories)
        std::string fullEntryPath = directoryPath;
        if (fullEntryPath.back() != '/') {
            fullEntryPath += '/';
        }
        fullEntryPath += name;

        struct stat entry_stat;
        if (stat(fullEntryPath.c_str(), &entry_stat) == 0) {
            if (S_ISREG(entry_stat.st_mode)) { // Check if it's a regular file
                files.push_back(name);
            }
        } else {
            SS_LOG_WARN("Failed to stat entry: " << fullEntryPath << " - " << strerror(errno));
            // Decide whether to continue or return error
        }
    }

    closedir(dir);
    SS_LOG_DEBUG("Listed " << files.size() << " regular files in directory: " << directoryPath);
    return Error::Errc::Success;
}


} // namespace Utils
} // namespace SecureStorage