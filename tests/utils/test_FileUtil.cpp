#include "gtest/gtest.h"
#include "FileUtil.h" // Assuming ss_utils makes this directly available
#include "Error.h"    // Same assumption
#include "Logger.h"   // Same assumption

#include <fstream>
#include <algorithm> // For std::equal
#include <cstdio>    // For BUFSIZ used in some temporary name generation (not strictly needed here)
#include <thread>    // For thread ID in temp dir name
#include <chrono>    // For timestamp in temp dir name
#include <random>    // For random number in temp dir name

// POSIX includes for rmdir, unlink, opendir etc. for cleanup if needed directly
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

namespace SecureStorage {
namespace Utils {
namespace Test {

// Helper function to recursively delete a directory
// Be very careful with such functions!
bool removeDirectoryRecursive(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        // If it doesn't exist, it's "removed" in a sense, or error opening
        return !FileUtil::pathExists(path);
    }

    struct dirent* entry;
    bool success = true;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }

        std::string fullEntryPath = path + "/" + name;
        struct stat entry_stat;
        if (stat(fullEntryPath.c_str(), &entry_stat) != 0) {
             SS_LOG_ERROR("Failed to stat: " << fullEntryPath);
             success = false; // could not stat, problem
             continue;
        }


        if (S_ISDIR(entry_stat.st_mode)) {
            if (!removeDirectoryRecursive(fullEntryPath)) {
                success = false;
            }
        } else {
            if (std::remove(fullEntryPath.c_str()) != 0) {
                SS_LOG_ERROR("Failed to remove file: " << fullEntryPath << " - " << strerror(errno));
                success = false;
            }
        }
    }
    closedir(dir);

    if (::rmdir(path.c_str()) != 0) {
        SS_LOG_ERROR("Failed to remove directory: " << path << " - " << strerror(errno));
        success = false;
    }
    return success;
}


class FileUtilTest : public ::testing::Test {
protected:
    std::string testDirBase; // Base for all FileUtil tests (e.g., build/FileUtilTests)
    std::string currentTestDir; // Specific dir for current test (e.g., build/FileUtilTests/TestName_PID_Time)

    static std::string generateUniqueDirName(const std::string& testName) {
        std::stringstream ss;
        auto now = std::chrono::high_resolution_clock::now();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(1000, 9999);

        ss << testName << "_"
           << std::this_thread::get_id() << "_"
           << nanos << "_" << distrib(gen);
        return ss.str();
    }

    void SetUp() override {
        // Create a base temporary directory for all tests if it doesn't exist.
        // Using a path relative to the build directory is safer.
        // CMAKE_BINARY_DIR is available at CMake configure time, not directly here.
        // Let's assume tests run from build/tests/utils or similar.
        // For simplicity, we create it in the current working directory,
        // assuming the test executable is run from a location where creating
        // a "FileUtilTestSandbox" directory is acceptable (e.g., build directory).
        testDirBase = "FileUtilTestSandbox"; // This will be created where the test executable runs

        // Ensure the base directory exists
        if (!FileUtil::pathExists(testDirBase)) {
            ASSERT_EQ(FileUtil::createDirectories(testDirBase), Error::Errc::Success)
                << "Failed to create base test directory: " << testDirBase;
        } else {
            struct stat st_base; // Create a named struct stat
            if (! ( (stat(testDirBase.c_str(), &st_base ) == 0) && S_ISDIR(st_base.st_mode) ) ) {
            // For robust test setup, delete it if it's a file.
            // For now, we'll assume if it exists, it's a directory we can use.
            SS_LOG_WARN("Base test directory " << testDirBase << " exists but is not a directory. Attempting to remove and recreate.");
            if (std::remove(testDirBase.c_str()) != 0 && FileUtil::pathExists(testDirBase)) {
                 FAIL() << "Could not remove existing non-directory file at base test path: " << testDirBase;
            }
            ASSERT_EQ(FileUtil::createDirectories(testDirBase), Error::Errc::Success)
                << "Failed to create base test directory after attempting to clean non-directory file: " << testDirBase;
            }
        }


        // Create a unique subdirectory for this specific test case
        const ::testing::TestInfo* const test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        currentTestDir = testDirBase + "/" + generateUniqueDirName(test_info->name());

        ASSERT_EQ(FileUtil::createDirectories(currentTestDir), Error::Errc::Success)
            << "Failed to create current test directory: " << currentTestDir;
        SS_LOG_INFO("Created test sandbox: " << currentTestDir);
    }

    void TearDown() override {
        SS_LOG_INFO("Cleaning up test sandbox: " << currentTestDir);
        // Recursively delete the current test's directory
        if (FileUtil::pathExists(currentTestDir)) {
            ASSERT_TRUE(removeDirectoryRecursive(currentTestDir))
                << "Failed to clean up test directory: " << currentTestDir;
        }
        // Optionally, clean up the base directory if no other tests are running
        // but that's harder to manage. Individual test case cleanup is safer.
    }

    std::string getTestFilePath(const std::string& filename) const {
        return currentTestDir + "/" + filename;
    }
};


TEST_F(FileUtilTest, PathExists) {
    std::string existingFile = getTestFilePath("exists.txt");
    std::ofstream ofs(existingFile);
    ASSERT_TRUE(ofs.good());
    ofs << "content";
    ofs.close();

    std::string existingDir = getTestFilePath("exists_dir");
    ASSERT_EQ(FileUtil::createDirectories(existingDir), Error::Errc::Success);

    ASSERT_TRUE(FileUtil::pathExists(existingFile));
    ASSERT_TRUE(FileUtil::pathExists(existingDir));
    ASSERT_FALSE(FileUtil::pathExists(getTestFilePath("nonexistent.txt")));
    ASSERT_FALSE(FileUtil::pathExists(""));
}

TEST_F(FileUtilTest, CreateDirectoriesSingleLevel) {
    std::string dir = getTestFilePath("new_dir");
    ASSERT_FALSE(FileUtil::pathExists(dir));
    ASSERT_EQ(FileUtil::createDirectories(dir), Error::Errc::Success);
    ASSERT_TRUE(FileUtil::pathExists(dir));
    struct stat st;
    ASSERT_EQ(stat(dir.c_str(), &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));
}

TEST_F(FileUtilTest, CreateDirectoriesMultiLevel) {
    std::string dir = getTestFilePath("parent/child/grandchild");
    ASSERT_FALSE(FileUtil::pathExists(dir));
    ASSERT_EQ(FileUtil::createDirectories(dir), Error::Errc::Success);
    ASSERT_TRUE(FileUtil::pathExists(dir));
    ASSERT_TRUE(FileUtil::pathExists(getTestFilePath("parent/child")));
    ASSERT_TRUE(FileUtil::pathExists(getTestFilePath("parent")));
}

TEST_F(FileUtilTest, CreateDirectoriesPathIsFile) {
    std::string filePath = getTestFilePath("iam_a_file.txt");
    std::ofstream ofs(filePath);
    ofs << "hello";
    ofs.close();
    ASSERT_TRUE(FileUtil::pathExists(filePath));

    // Attempting to create a directory where a file exists should fail
    ASSERT_NE(FileUtil::createDirectories(filePath), Error::Errc::Success);
}

TEST_F(FileUtilTest, CreateDirectoriesEmptyPath) {
    ASSERT_EQ(FileUtil::createDirectories(""), Error::Errc::InvalidArgument);
}


TEST_F(FileUtilTest, AtomicWriteAndReadFile) {
    std::string filepath = getTestFilePath("atomic_test.dat");
    std::vector<unsigned char>writeData = {'t', 'e', 's', 't', ' ', 'd', 'a', 't', 'a'};

    ASSERT_EQ(FileUtil::atomicWriteFile(filepath, writeData), Error::Errc::Success);
    ASSERT_TRUE(FileUtil::pathExists(filepath));

    std::vector<unsigned char> readData;
    ASSERT_EQ(FileUtil::readFile(filepath, readData), Error::Errc::Success);
    ASSERT_EQ(readData, writeData);
}

TEST_F(FileUtilTest, AtomicWriteEmptyFile) {
    std::string filepath = getTestFilePath("atomic_empty.dat");
    std::vector<unsigned char>writeData; // Empty data

    ASSERT_EQ(FileUtil::atomicWriteFile(filepath, writeData), Error::Errc::Success);
    ASSERT_TRUE(FileUtil::pathExists(filepath));

    std::vector<unsigned char> readData;
    ASSERT_EQ(FileUtil::readFile(filepath, readData), Error::Errc::Success);
    ASSERT_TRUE(readData.empty());
}

TEST_F(FileUtilTest, AtomicWriteOverwrite) {
    std::string filepath = getTestFilePath("atomic_overwrite.dat");
    std::vector<unsigned char> initialData = {'o', 'l', 'd'};
    std::vector<unsigned char> newData = {'n', 'e', 'w'};

    ASSERT_EQ(FileUtil::atomicWriteFile(filepath, initialData), Error::Errc::Success);
    ASSERT_EQ(FileUtil::atomicWriteFile(filepath, newData), Error::Errc::Success);

    std::vector<unsigned char> readData;
    ASSERT_EQ(FileUtil::readFile(filepath, readData), Error::Errc::Success);
    ASSERT_EQ(readData, newData);
}

TEST_F(FileUtilTest, ReadFileNotExists) {
    std::string filepath = getTestFilePath("non_existent_read.dat");
    std::vector<unsigned char> data;
    ASSERT_EQ(FileUtil::readFile(filepath, data), Error::Errc::FileOpenFailed); // Or PathNotFound depending on impl.
    ASSERT_TRUE(data.empty());
}

TEST_F(FileUtilTest, DeleteFile) {
    std::string filepath = getTestFilePath("to_delete.txt");
    std::ofstream(filepath) << "content"; // Create file
    ASSERT_TRUE(FileUtil::pathExists(filepath));

    ASSERT_EQ(FileUtil::deleteFile(filepath), Error::Errc::Success);
    ASSERT_FALSE(FileUtil::pathExists(filepath));
}

TEST_F(FileUtilTest, DeleteFileNotExists) {
    std::string filepath = getTestFilePath("already_gone.txt");
    ASSERT_FALSE(FileUtil::pathExists(filepath));
    ASSERT_EQ(FileUtil::deleteFile(filepath), Error::Errc::Success); // Should be success
}

TEST_F(FileUtilTest, GetDirectory) {
    ASSERT_EQ(FileUtil::getDirectory("/usr/local/bin/file.txt"), "/usr/local/bin");
    ASSERT_EQ(FileUtil::getDirectory("relative/path/to/file.doc"), "relative/path/to");
    ASSERT_EQ(FileUtil::getDirectory("filename_only.cpp"), ""); // Or "." depending on desired behavior
    ASSERT_EQ(FileUtil::getDirectory("/a/b/c/"), "/a/b/c"); // Trailing slash
    ASSERT_EQ(FileUtil::getDirectory("/"), "/"); // Or "/" if preferred
    ASSERT_EQ(FileUtil::getDirectory(""), "");
}

TEST_F(FileUtilTest, ListDirectory) {
    std::string dirToList = getTestFilePath("list_test_dir");
    ASSERT_EQ(FileUtil::createDirectories(dirToList), Error::Errc::Success);

    std::ofstream(dirToList + "/file1.txt") << "f1";
    std::ofstream(dirToList + "/file2.dat") << "f2";
    ASSERT_EQ(FileUtil::createDirectories(dirToList + "/subdir"), Error::Errc::Success);
    std::ofstream(dirToList + "/subdir/file_in_subdir.txt") << "f_sub";


    std::vector<std::string> files;
    ASSERT_EQ(FileUtil::listDirectory(dirToList, files), Error::Errc::Success);

    // Sort for consistent comparison
    std::sort(files.begin(), files.end());

    ASSERT_EQ(files.size(), 2);
    if (files.size() == 2) {
        EXPECT_EQ(files[0], "file1.txt");
        EXPECT_EQ(files[1], "file2.dat");
    }
}

TEST_F(FileUtilTest, ListDirectoryEmpty) {
    std::string dirToList = getTestFilePath("empty_list_dir");
    ASSERT_EQ(FileUtil::createDirectories(dirToList), Error::Errc::Success);

    std::vector<std::string> files;
    ASSERT_EQ(FileUtil::listDirectory(dirToList, files), Error::Errc::Success);
    ASSERT_TRUE(files.empty());
}

TEST_F(FileUtilTest, ListDirectoryNotExists) {
    std::string dirToList = getTestFilePath("non_existent_list_dir");
    std::vector<std::string> files;
    ASSERT_EQ(FileUtil::listDirectory(dirToList, files), Error::Errc::FileOpenFailed);
}


} // namespace Test
} // namespace Utils
} // namespace SecureStorage