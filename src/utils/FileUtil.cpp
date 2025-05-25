#include "FileUtil.h"
#include "Logger.h" // For SS_LOG_ macros

#include <cerrno>     // For errno
#include <cstdio>     // For std::remove, std::rename
#include <cstring>    // For strerror
#include <sys/stat.h> // For mkdir, stat
#ifdef _WIN32
// Windows-specific includes
#include <direct.h>  // For _mkdir
#include <io.h>      // For _commit (Windows equivalent of fsync)
#include <windows.h> // For CreateDirectory and other Windows-specific file operations
#else
// POSIX-specific includes
#include <dirent.h> // For directory listings
#include <fcntl.h>  // For open flags (O_WRONLY, O_CREAT, O_TRUNC, O_RDONLY, O_DIRECTORY)
#include <unistd.h> // For fsync, close, write, open
#endif

#include <sys/types.h>

namespace SecureStorage {
namespace Utils {

std::string FileUtil::getDirectory(const std::string &filepath) {
    size_t last_slash_idx = filepath.find_last_of("/\\");
    if (std::string::npos != last_slash_idx) {
        if (last_slash_idx == 0) {
            return filepath.substr(0, 1);
        }
        return filepath.substr(0, last_slash_idx);
    }
    return "";
}

Error::Errc FileUtil::atomicWriteFile(const std::string &filepath,
                                      const std::vector<unsigned char> &data) {
    if (filepath.empty()) {
        SS_LOG_ERROR("Filepath for atomic write is empty.");
        return Error::Errc::InvalidArgument;
    }

    std::string outputDir = getDirectory(filepath);
    if (!outputDir.empty()) {
        if (!pathExists(outputDir)) {
            SS_LOG_DEBUG("Attempting to create output directory: " << outputDir);
            Error::Errc dirErr = createDirectories(outputDir);
            if (dirErr != Error::Errc::Success) {
                SS_LOG_ERROR(
                    "Failed to create directory '"
                    << outputDir << "' for file '" << filepath << "'. Error: "
                    << Error::SecureStorageErrorCategory::get().message(static_cast<int>(dirErr)));
                return dirErr;
            }
            SS_LOG_INFO("Successfully created directory: " << outputDir);
        } else {
            struct stat st;
            if (stat(outputDir.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) {
                SS_LOG_ERROR("Output path '" << outputDir << "' exists but is not a directory.");
                return Error::Errc::OperationFailed;
            }
        }
    }

    std::string tempFilepath = filepath + TEMP_FILE_UTIL_SUFFIX;

    int fd = -1;
#ifdef _WIN32
    {
        std::ofstream ofs(tempFilepath, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open() || !ofs.good()) {
            SS_LOG_ERROR("Failed to open temporary file for writing (Windows fallback): "
                         << tempFilepath << " - " << strerror(errno));
            return Error::Errc::FileOpenFailed;
        }
        if (!data.empty()) {
            ofs.write(reinterpret_cast<const char *>(data.data()), data.size());
            if (!ofs.good()) {
                SS_LOG_ERROR("Failed to write data to temporary file (Windows fallback): "
                             << tempFilepath << " - " << strerror(errno));
                ofs.close();
                std::remove(tempFilepath.c_str());
                return Error::Errc::FileWriteFailed;
            }
        }
        ofs.flush();
        ofs.close();
        if (ofs.fail() && !ofs.eof()) {
            SS_LOG_ERROR("Error during close after writing temporary file (Windows fallback): "
                         << tempFilepath << " - " << strerror(errno));
            std::remove(tempFilepath.c_str());
            return Error::Errc::FileWriteFailed;
        }
    }
#else // POSIX
    fd = open(tempFilepath.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
              S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); // Permissions 0644
    if (fd < 0) {
        SS_LOG_ERROR("Failed to open temporary file '" << tempFilepath
                                                       << "' for writing: " << strerror(errno));
        return Error::Errc::FileOpenFailed;
    }

    if (!data.empty()) {
        ssize_t bytes_written = write(fd, data.data(), data.size());
        if (bytes_written < 0 || static_cast<size_t>(bytes_written) != data.size()) {
            SS_LOG_ERROR("Failed to write data to temporary file '" << tempFilepath
                                                                    << "': " << strerror(errno));
            close(fd);
            std::remove(tempFilepath.c_str());
            return Error::Errc::FileWriteFailed;
        }
    }

    if (fsync(fd) != 0) {
        SS_LOG_ERROR("Failed to fsync temporary file '" << tempFilepath
                                                        << "': " << strerror(errno));
        close(fd);
        std::remove(tempFilepath.c_str());
        return Error::Errc::FileWriteFailed;
    }

    if (close(fd) != 0) {
        SS_LOG_ERROR("Failed to close temporary file '" << tempFilepath
                                                        << "' after fsync: " << strerror(errno));
        std::remove(tempFilepath.c_str());
        return Error::Errc::FileWriteFailed;
    }
#endif
    SS_LOG_DEBUG("Successfully wrote and synced data to temporary file: " << tempFilepath);

    if (std::rename(tempFilepath.c_str(), filepath.c_str()) != 0) {
        SS_LOG_ERROR("Failed to rename temporary file '" << tempFilepath << "' to '" << filepath
                                                         << "' - " << strerror(errno));
        std::remove(tempFilepath.c_str());
        return Error::Errc::FileRenameFailed;
    }
    SS_LOG_DEBUG("Successfully renamed temp file to: " << filepath);

#ifndef _WIN32
    std::string dirToSync = outputDir;
    if (dirToSync.empty()) { // File is in current directory
        dirToSync = ".";
    }

    int dir_fd = open(dirToSync.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0) {
        SS_LOG_WARN("Failed to open directory '"
                    << dirToSync << "' for fsync: " << strerror(errno)
                    << ". Rename operation might not be fully persistent on power loss.");
    } else {
        if (fsync(dir_fd) != 0) {
            SS_LOG_WARN("Failed to fsync directory '"
                        << dirToSync << "': " << strerror(errno)
                        << ". Rename operation might not be fully persistent on power loss.");
        }
        close(dir_fd);
        SS_LOG_DEBUG("Successfully fsynced directory: " << dirToSync);
    }
#else
    SS_LOG_DEBUG("Directory fsync step skipped on Windows for atomicWriteFile.");
#endif

    return Error::Errc::Success;
}

Error::Errc FileUtil::readFile(const std::string &filepath, std::vector<unsigned char> &data) {
    data.clear();
    if (filepath.empty()) {
        SS_LOG_ERROR("Filepath for read is empty.");
        return Error::Errc::InvalidArgument;
    }

    std::ifstream ifs(filepath, std::ios::binary | std::ios::ate);
    if (!ifs.is_open() || !ifs.good()) {
        SS_LOG_DEBUG("Failed to open file for reading: " << filepath << " - " << strerror(errno));
        return Error::Errc::FileOpenFailed;
    }

    std::streamsize size = ifs.tellg();
    if (size < 0) {
        SS_LOG_ERROR("Failed to determine size of file: " << filepath);
        ifs.close();
        return Error::Errc::FileReadFailed;
    }
    ifs.seekg(0, std::ios::beg);

    if (size == 0) {
        SS_LOG_DEBUG("File is empty: " << filepath);
        ifs.close();
        return Error::Errc::Success;
    }

    data.resize(static_cast<size_t>(size));
    if (!data.empty()) {
        ifs.read(reinterpret_cast<char *>(data.data()), size);
        if (!ifs.good() && !ifs.eof()) {
            SS_LOG_ERROR("Failed to read data from file: " << filepath << " - Read " << ifs.gcount()
                                                           << " of " << size
                                                           << " bytes. Error: " << strerror(errno));
            data.clear();
            ifs.close();
            return Error::Errc::FileReadFailed;
        }
    }
    ifs.close();
    SS_LOG_DEBUG("Successfully read " << data.size() << " bytes from file: " << filepath);
    return Error::Errc::Success;
}

Error::Errc FileUtil::deleteFile(const std::string &filepath) {
    if (filepath.empty()) {
        SS_LOG_ERROR("Filepath for delete is empty.");
        return Error::Errc::InvalidArgument;
    }
    if (!pathExists(filepath)) {
        SS_LOG_DEBUG("File to delete does not exist, no action needed: " << filepath);
        return Error::Errc::Success;
    }

    if (std::remove(filepath.c_str()) != 0) {
        SS_LOG_ERROR("Failed to delete file: " << filepath << " - " << strerror(errno));
        return Error::Errc::FileRemoveFailed;
    }
    SS_LOG_DEBUG("Successfully deleted file: " << filepath);
    return Error::Errc::Success;
}

bool FileUtil::pathExists(const std::string &filepath) {
    if (filepath.empty()) {
        return false;
    }
    struct stat buffer;
    return (stat(filepath.c_str(), &buffer) == 0);
}

Error::Errc FileUtil::createDirectories(const std::string &path) {
    if (path.empty()) {
        SS_LOG_ERROR("Path for createDirectories is empty.");
        return Error::Errc::InvalidArgument;
    }

    struct stat st;
    if (stat(path.c_str(), &st) == 0) { // Path exists
        if (S_ISDIR(st.st_mode)) {
            SS_LOG_DEBUG("Path already exists and is a directory: " << path);
            return Error::Errc::Success;
        } else {
            SS_LOG_ERROR("Path exists but is not a directory: " << path);
            return Error::Errc::OperationFailed;
        }
    }

    // Path does not exist, try to create it.
    // Iterate through path segments and create them one by one.
    // This version is simplified for POSIX and assumes '/' as separator.
    // For Windows, a more elaborate approach handling '\' and drive letters would be needed.
    std::string currentPath;
    size_t pos = 0;
    std::string path_copy = path; // Use a copy to handle potential trailing slash

    // Normalize path separators to '/' for internal processing, especially if mixed ones could come
    // from tests.
    for (char &ch : path_copy) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    // Remove trailing slash if present, unless it's the root "/"
    if (path_copy.length() > 1 && path_copy.back() == '/') {
        path_copy.pop_back();
    }

    // Handle absolute paths starting with '/'
    if (!path_copy.empty() && path_copy[0] == '/') {
        // If root "/" itself is the path, and it doesn't exist (unlikely on POSIX),
        // mkdir would fail. But usually "/" exists.
        // We don't try to create "/", just path components within it.
        pos = 1; // Start search for next '/' after the initial one
    }

    while (pos < path_copy.length()) {
        size_t next_slash = path_copy.find('/', pos);
        if (next_slash == std::string::npos) { // Last segment
            currentPath = path_copy;
            pos = path_copy.length(); // To exit loop
        } else {
            currentPath = path_copy.substr(0, next_slash);
            pos = next_slash + 1;
            if (currentPath.empty() && path_copy[0] == '/') { // Handle paths like "/foo"
                currentPath = path_copy.substr(0, next_slash == 0 ? 1 : next_slash);
            } else if (currentPath.empty()) { // Should not happen with corrected logic if path_copy
                                              // is not empty
                continue;
            }
        }

        if (currentPath == "." || currentPath == "..")
            continue; // Skip . and ..

        if (stat(currentPath.c_str(), &st) != 0) { // If current segment path doesn't exist
            SS_LOG_DEBUG("Attempting to create directory component: " << currentPath);
#ifdef _WIN32
            if (_mkdir(currentPath.c_str()) != 0 && errno != EEXIST) {
#else
            if (mkdir(currentPath.c_str(), 0755) != 0 && errno != EEXIST) { // 0755 permissions
#endif
                SS_LOG_ERROR("Failed to create directory: " << currentPath << " - "
                                                            << strerror(errno));
                return Error::Errc::OperationFailed;
            }
            SS_LOG_DEBUG("Created directory component: " << currentPath);
        } else if (!S_ISDIR(st.st_mode)) { // Path exists but is not a directory
            SS_LOG_ERROR("Path component exists but is not a directory: " << currentPath);
            return Error::Errc::OperationFailed;
        }
        // else: path exists and is a directory, continue
    }

    // Final check on the full path
    if (stat(path_copy.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        SS_LOG_ERROR("Directory creation appears to have failed for final path: " << path_copy);
        return Error::Errc::OperationFailed;
    }

    SS_LOG_DEBUG("Successfully ensured directory exists: " << path_copy);
    return Error::Errc::Success;
}

Error::Errc FileUtil::listDirectory(const std::string &directoryPath,
                                    std::vector<std::string> &files) {
    files.clear();
    if (directoryPath.empty()) {
        SS_LOG_ERROR("Directory path for listing is empty.");
        return Error::Errc::InvalidArgument;
    }

#ifdef _WIN32
    WIN32_FIND_DATAA findFileData;
    HANDLE hFind = FindFirstFileA((directoryPath + "\\*").c_str(), &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        SS_LOG_ERROR("Failed to open directory (Windows): " << directoryPath
                                                            << " - Error: " << GetLastError());
        return Error::Errc::FileOpenFailed;
    }
    do {
        std::string name = findFileData.cFileName;
        if (name != "." && name != "..") {
            // To ensure we only list files, check attributes
            std::string fullEntryPath = directoryPath;
            if (!fullEntryPath.empty() && fullEntryPath.back() != '\\' &&
                fullEntryPath.back() != '/') {
                fullEntryPath += '\\';
            }
            fullEntryPath += name;

            DWORD attributes = GetFileAttributesA(fullEntryPath.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(name);
            }
        }
    } while (FindNextFileA(hFind, &findFileData) != 0);
    FindClose(hFind);
#else
    DIR *dir = opendir(directoryPath.c_str());
    if (dir == nullptr) {
        SS_LOG_ERROR("Failed to open directory: " << directoryPath << " - " << strerror(errno));
        return Error::Errc::FileOpenFailed;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }

        std::string fullEntryPath = directoryPath;
        if (!fullEntryPath.empty() && fullEntryPath.back() != '/') {
            fullEntryPath += '/';
        }
        fullEntryPath += name;

        struct stat entry_stat;
        if (stat(fullEntryPath.c_str(), &entry_stat) == 0) {
            if (S_ISREG(entry_stat.st_mode)) {
                files.push_back(name);
            }
        } else {
            SS_LOG_WARN("Failed to stat entry: " << fullEntryPath << " - " << strerror(errno));
        }
    }
    closedir(dir);
#endif
    SS_LOG_DEBUG("Listed " << files.size() << " regular files in directory: " << directoryPath);
    return Error::Errc::Success;
}

} // namespace Utils
} // namespace SecureStorage