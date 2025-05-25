# SecureStorage Library

## System Requirements and Project Proposal

Refer to this document for complete details, requirements, and specifications followed in this implementation:

[PROJECT PROPOSAL/SYSTEM REQUIREMENTS](docs/project_proposal.md)

## Overview

The SecureStorage library provides C++11 compatible mechanisms for robustly encrypting,
decrypting, and securely storing data on disk. It is specifically designed with
low memory and CPU footprints in mind, making it suitable for resource-constrained
environments like automotive custom Linux hardware and Android 18 based displays.

The library focuses on data confidentiality through AES-256-GCM authenticated
encryption, device-specific key derivation (preventing direct key storage),
data integrity, resilience against power key cycles via atomic file operations and backups,
and monitoring of the storage area for unintended modifications. It operates entirely
offline.

## Key Features

* **Strong Encryption:** AES-256-GCM for confidentiality and integrity.
* **Device-Specific Keys:** Uses HKDF to derive unique encryption keys from a device serial number. Keys are not stored.
* **Managed Secure Storage:** Provides an API (`SecureStorageManager`) to store and retrieve data items by ID within a designated secure directory.
* **Atomic File Operations:** Ensures data resilience during writes using write-to-temporary-then-rename strategies.
* **Backup & Restore:** Automatically manages backup copies of data files and attempts to restore from them if primary files are corrupted.
* **File Watcher:** Continuously monitors the secure storage directory for external modifications (e.g., unintended writes, deletions) and logs these events. (Linux `inotify` based).
* **Cross-Platform C++11:** Designed for portability to automotive Linux and older Android NDK environments.
* **Low Footprint:** Conscious of resource usage.
* **Error Handling:** Clear error codes via `SecureStorage::Error::Errc` (compatible with `std::error_code`).
* **Offline Operation:** No internet connectivity required.
* **CMake Build System:** Includes `WorkspaceContent` for Mbed TLS and Google Test dependencies.
* **Unit Tested:** Includes a suite of unit tests for core components.
* **Code Quality Tools:** Supports `clang-format` for formatting, `clang-tidy` and `cppcheck` for static analysis, and `lcov` for code coverage.

## Build Prerequisites (for Local Development)

* **Operating System:** Linux (Debian/Ubuntu based recommended for easy package installation).
* **Core Build Tools:**
  * CMake (version 3.14 or higher)
  * A C++11 compatible compiler (e.g., GCC, Clang) - `build-essential` on Debian/Ubuntu.
* **Git:** For cloning the repository and `WorkspaceContent`.
* **Code Formatting:** `clang-format`
* **Static Analysis:** `clang-tidy`, `cppcheck`
* **Code Coverage (for GCC):** `lcov` (gcov is part of GCC)
* **Documentation Generation:** `doxygen`, `graphviz`

Mbed TLS and Google Test (for tests) are fetched automatically by CMake via `WorkspaceContent` during the CMake configuration step.

## Local Development Environment Setup (Linux - Debian/Ubuntu)

1. **Update Package List:**

    ```bash
    sudo apt update
    ```

2. **Install Core Build Tools:**

    ```bash
    sudo apt install -y build-essential cmake git
    ```

3. **Install Clang Tools (Compiler, Formatter, Tidy):**

    ```bash
    sudo apt install -y clang llvm clang-format clang-tidy
    ```

4. **Install Cppcheck:**

    ```bash
    sudo apt install -y cppcheck
    ```

5. **Install Doxygen and Graphviz (for Documentation):**

    ```bash
    sudo apt install -y doxygen graphviz
    ```

6. **Install LCOV (for GCC Code Coverage):**

    ```bash
    sudo apt install -y lcov
    ```

## Build Instructions (Local)

1. **Clone the Repository:**

    ```bash
    git clone <repository_url>
    cd SecureStorage # Or your repository name
    ```

2. **Configure with CMake (Create a build directory):**

    It's recommended to build out-of-source.

    ```bash
    mkdir build
    cd build
    cmake ..
    ```

    * This step will also fetch Mbed TLS and Google Test if they haven't been downloaded yet.
    * Your root `CMakeLists.txt` should have `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)` to generate `compile_commands.json` in the `build` directory, which is used by `clang-tidy` and `cppcheck`.

3. **Build the Library, Examples, and Tests:**
    From the `build` directory:

    ```bash
    make -j$(nproc)
    # Or: cmake --build . -j$(nproc)
    ```

4. **Run Unit Tests:**
    From the `build` directory:

    ```bash
    ctest --output-on-failure -V
    ```

    * Ensure `enable_testing()` is called in your root `CMakeLists.txt` for `ctest` to work from the top-level build directory.

## Local Code Quality and Analysis

It's highly recommended to run these tools locally before committing changes.

### 1. Code Formatting (`clang-format`)

This project uses `clang-format` to maintain a consistent code style. A `.clang-format` configuration file is provided in the root of the repository.

* **Check if a file needs formatting (dry run):**

    ```bash
    # From the project root directory
    clang-format --style=file -n src/storage/SecureStore.cpp
    ```

* **Apply formatting to a file:**

    ```bash
    clang-format --style=file -i src/storage/SecureStore.cpp
    ```

* **Format all C++ source and header files in `src/`, `tests/`, and `examples/` directories:**

    ```bash
    # From the project root directory
    find src/ tests/ examples/ \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -exec clang-format --style=file -i {} \;
    ```

    **Caution:** This command modifies files directly. Ensure your work is committed or backed up.

### 2. Static Analysis (`clang-tidy`)

`clang-tidy` provides deeper static analysis based on the Clang AST. A `.clang-tidy` configuration file in the project root guides its checks.

1. **Ensure `compile_commands.json` is generated:** Run `cmake ..` in your `build` directory.
2. **Run `clang-tidy` on a specific file:**

    ```bash
    # From the project root directory, assuming 'build' is your build directory
    clang-tidy -p build src/storage/SecureStore.cpp
    ```

3. **Run `clang-tidy` during compilation with Clang (recommended for full project analysis):**
    Configure CMake to use Clang and enable `clang-tidy`:

    ```bash
    # From the project root directory
    mkdir build-clang-tidy && cd build-clang-tidy
    cmake .. -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_CLANG_TIDY=clang-tidy
    make -j$(nproc)
    ```

    Warnings and errors from `clang-tidy` will appear during the compilation process.

### 3. Static Analysis (`cppcheck`)

`cppcheck` is another static analysis tool that can find various bugs.

1. **Ensure `compile_commands.json` is generated:** Run `cmake ..` in your `build` directory.
2. **Run `cppcheck` on your source code (recommended way, using compilation database):**

    ```bash
    # From the project root directory
    cppcheck --project=build/compile_commands.json --enable=all --xml --output-file=cppcheck-local-report.xml src/
    ```

    * `--enable=all`: Enables a wide range of checks (can be verbose). Consider refining with specific checks like `--enable=warning,performance,portability`.
    * `--xml`: Outputs the report in XML format.
    * `src/`: Focuses the analysis on your source directory, using the project-wide compilation flags.

3. **View the report:** You can inspect `cppcheck-local-report.xml` or use a viewer if available.

### 4. Code Coverage (with GCC and `lcov`)

Generate code coverage reports to see how much of your code is exercised by your unit tests.

1. **Configure CMake for a Debug build with GCC and coverage flags:**
    From the project root:

    ```bash
    mkdir build-coverage && cd build-coverage
    cmake .. -DCMAKE_BUILD_TYPE=Debug \
             -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
             -DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage" \
             -DCMAKE_C_FLAGS="--coverage -fprofile-arcs -ftest-coverage" \
             -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
             -DCMAKE_SHARED_LINKER_FLAGS="--coverage"
    ```

2. **Build the project:**

    ```bash
    # Still in build-coverage directory
    make -j$(nproc)
    ```

3. **Run your unit tests:**
    This step is crucial as it generates the `.gcda` coverage data files.

    ```bash
    # Still in build-coverage directory
    ctest --output-on-failure
    ```

4. **Generate the HTML coverage report using `lcov` and `genhtml`:**

    ```bash
    # Still in build-coverage directory
    lcov --capture --directory . --output-file coverage.info --rc lcov_branch_coverage=1
    
    # Filter out system headers, external dependencies, tests, and examples
    lcov --remove coverage.info '/usr/*' --output-file coverage.info --rc lcov_branch_coverage=1
    lcov --remove coverage.info "$(pwd)/_deps/*" --output-file coverage.info --rc lcov_branch_coverage=1 # For FetchContent
    lcov --remove coverage.info "${PWD}/../tests/*" --output-file coverage.info --rc lcov_branch_coverage=1 # Assuming build-coverage is one level down
    lcov --remove coverage.info "${PWD}/../examples/*" --output-file coverage.info --rc lcov_branch_coverage=1
    
    genhtml coverage.info --output-directory coverage-html --title "SecureStorage Local Coverage" --legend --branch-coverage
    ```

5. **View the report:**
    Open `build-coverage/coverage-html/index.html` in your web browser.

    ```bash
    xdg-open coverage-html/index.html
    ```

### 5. API Documentation (`Doxygen` & `Graphviz`)

Generate HTML documentation from your source code comments.

1. **Ensure you have a `Doxyfile.in` in your project root.**
2. **Run CMake in your build directory** (e.g., `build/`) if you haven't already: `cmake ..`
    This generates `build/Doxyfile` from `Doxyfile.in`.
3. **Generate documentation using the Doxygen target from CMake:**
    From your `build` directory:

    ```bash
    make doxygen
    # Or: cmake --build . --target doxygen
    ```

    (This assumes the Doxygen target is configured to run with `ALL` or you explicitly call it).
4. **Alternatively, run Doxygen manually:**
    From your `build` directory (where `Doxyfile` was generated):

    ```bash
    doxygen Doxyfile
    ```

5. **View the documentation:**
    The output path is configured in your `Doxyfile.in` (e.g., `HTML_OUTPUT = html`) relative to the `OUTPUT_DIRECTORY` (e.g., `docs/` inside your build directory). A common path would be `build/docs/html/index.html`.

    ```bash
    xdg-open docs/html/index.html # Adjust path if necessary
    ```

## Continuous Integration (CI)

## Continuous Integration (CI), Analysis, and Releases

This project uses GitHub Actions for Continuous Integration (CI), code analysis, and release management. The workflow is defined in [ci.yml](.github/workflows/ci.yml).

Key CI pipeline features include:

* **Multi-Platform Builds:**
  * Linux builds using GCC (with code coverage enabled) and Clang (with `clang-tidy` analysis enabled).
  * Android builds for API levels 18 (armeabi-v7a) and 21 (arm64-v8a).
* **Static Code Analysis:**
  * **`clang-tidy`**: Integrated into the Clang build on Linux for in-depth static analysis. Configuration is typically managed via a `.clang-tidy` file in the repository root.
  * **`cppcheck`**: Runs on Linux builds (both GCC and Clang configurations) to find potential bugs. Reports are uploaded as build artifacts.
* **Code Coverage:**
  * For GCC builds on Linux, code coverage is measured using `gcov`/`lcov`.
  * HTML coverage reports are generated and uploaded as build artifacts, allowing review of test effectiveness.
* **Documentation Generation:**
  * `Doxygen` (with `Graphviz`) is used to generate API documentation from source code comments.
  * The HTML documentation is uploaded as a build artifact.
* **Unit Testing:**
  * Tests are executed on Linux builds as part of the CI pipeline using CTest.
* **Automated Releases:**
  * When a tag matching the pattern `v*` (e.g., `v1.0.0`) is pushed, or when manually triggered via `workflow_dispatch`, a GitHub Release is created.
  * The release includes packaged build artifacts for all supported platforms (Linux, Android), Doxygen documentation, code coverage reports, and static analysis reports.
  * Manual triggers create draft releases for testing purposes.

The CI workflow ensures that every push and pull request is automatically built and tested, and that releases are consistently packaged with all relevant artifacts.

## Basic Usage (`SecureStorageManager`)

The primary interface to the library is the `SecureStorage::SecureStorageManager` class.

```cpp
#include "SecureStorageManager.h" // Main library header (adjust path if needed)
#include "utils/Logger.h" // For logger
#include "utils/Error.h"  // For Error::Errc
#include "utils/FileUtil.h" // For creating example directory
#include "file_watcher/FileWatcher.h" // For WatchedEvent
#include <iostream>
#include <vector>
#include <string>
#include <thread> // For std::this_thread::sleep_for
#include <chrono> // For std::chrono
#include <fstream> // For std::ofstream

// Example FileWatcher callback
void myFileWatcherCallback(const SecureStorage::FileWatcher::WatchedEvent& event) {
    std::cout << "[Watcher Callback] Event on: " << event.filePath
              << (event.fileName.empty() ? "" : "/" + event.fileName)
              << " Mask: 0x" << std::hex << event.mask << std::dec
              << " (" << event.eventNameStr << ")" << std::endl;
}

int main() {
    // Configure the library's logger (optional)
    SecureStorage::Utils::Logger::getInstance().setLogLevel(SecureStorage::Utils::LogLevel::DEBUG);

    std::string app_root_storage = "./app_secure_files_example"; // Choose an appropriate path
    std::string device_unique_serial = "MyDeviceSN001"; // Use the actual unique device serial

    // Ensure the directory exists for the example
    SecureStorage::Utils::FileUtil::createDirectories(app_root_storage);


    SecureStorage::SecureStorageManager manager(app_root_storage, device_unique_serial, myFileWatcherCallback);

    if (!manager.isInitialized()) {
        SS_LOG_ERROR("SecureStorageManager failed to initialize.");
        return 1;
    }
    SS_LOG_INFO("SecureStorageManager initialized.");
    if (manager.isFileWatcherActive()) {
        SS_LOG_INFO("File watcher is active on: " << app_root_storage);
    } else {
        SS_LOG_WARN("File watcher is NOT active.");
    }

    std::string data_id = "user_settings";
    std::vector<unsigned char> data_to_save = {'s', 'e', 'c', 'r', 'e', 't', ' ', 'd', 'a', 't', 'a'};

    // Store data
    if (manager.storeData(data_id, data_to_save) == SecureStorage::Error::Errc::Success) {
        SS_LOG_INFO("Data stored successfully for ID: " << data_id);
    } else {
        SS_LOG_ERROR("Failed to store data for ID: " << data_id);
    }

    // Retrieve data
    std::vector<unsigned char> retrieved_data;
    if (manager.retrieveData(data_id, retrieved_data) == SecureStorage::Error::Errc::Success) {
        std::string retrieved_str(retrieved_data.begin(), retrieved_data.end());
        SS_LOG_INFO("Retrieved data for ID " << data_id << ": " << retrieved_str);
    } else {
        SS_LOG_ERROR("Failed to retrieve data for ID: " << data_id);
    }

    // Check data existence
    if (manager.dataExists(data_id)) {
        SS_LOG_INFO("Data for ID " << data_id << " exists.");
    }

    // List IDs
    std::vector<std::string> ids;
    manager.listDataIds(ids);
    std::cout << "Stored IDs: ";
    for (const auto& id_item : ids) { std::cout << id_item << " "; }
    std::cout << std::endl;
    
    // Example: Let the watcher see an external change (for demonstration)
    // In a real app, you wouldn't typically do this externally if the manager is active.
    if (manager.isFileWatcherActive()) {
        std::cout << "Simulating external change for watcher (sleeping for 2s)..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1)); // Give watcher time to settle
        std::ofstream external_touch(app_root_storage + "/external_touch.txt");
        external_touch << "touched";
        external_touch.close();
        std::this_thread::sleep_for(std::chrono::seconds(1)); // Give watcher time to report
        SecureStorage::Utils::FileUtil::deleteFile(app_root_storage + "/external_touch.txt");
        std::this_thread::sleep_for(std::chrono::seconds(1)); 
    }
    
    // Delete data
    // manager.deleteData(data_id);

    // The manager's destructor will automatically stop the file watcher.
    std::cout << "Exiting example." << std::endl;
    return 0;
}
```

See also the examples/ directory for a command-line encryption/decryption utility using the library's components.

## API Documentation

Detailed API documentation can be generated using Doxygen.
If built locally (e.g., cmake --build build --target doxygen), you can typically find the documentation at build/docs/html/index.html (the exact path depends on your Doxyfile.in HTML_OUTPUT setting and CMake DOXYGEN_OUTPUT_DIR).

## Security Model

* Key Derivation: Encryption keys are derived at runtime using HKDF (HMAC-SHA256) from the provided device serial number and internal salts. The actual encryption key is not stored on the device, enhancing security.

* Encryption Algorithm: AES-256-GCM is used, providing strong 256-bit symmetric encryption with Galois/Counter Mode, which includes authentication (GMAC) to ensure data integrity and authenticity.

* Serial Number: The security of this system heavily relies on the uniqueness and inaccessibility of the device serial number to unauthorized parties.

* Offline: The library is designed for devices without internet access, reducing exposure to network-based attacks.

## File Watcher

The integrated file watcher monitors the root storage directory specified during SecureStorageManager initialization. It logs events such as:

* IN_CLOSE_WRITE: A file was closed after being opened for writing.
* IN_MODIFY: A file's content was modified.
* IN_DELETE: A file was deleted from the directory.
* IN_CREATE: A new file was created in the directory.
* Other relevant events like attribute changes or moves.

Logs from the file watcher use the library's internal logger (SS_LOG_INFO).

## Error Handling

Functions typically return a SecureStorage::Error::Errc enum value, which is compatible with std::error_code. Errc::Success (value 0) indicates success. Other values indicate specific errors. Refer to src/utils/Error.h for detailed error codes and their meanings.

## Backup and Restore

The SecureStore component (used by SecureStorageManager) implements a backup strategy:

* When data is stored (storeData), if a previous version of the data exists, it is typically moved to a backup file (e.g., data_id.enc.bak).
* When data is retrieved (retrieveData), if the primary data file (data_id.enc) is missing or fails decryption, the library automatically attempts to use the backup file.
* If the backup file is successfully used, an attempt is made to restore it as the primary file.

## Key Design Points

Some of the key design points are listed in docs directory, see the [KEY_DESIGN_POINTS](docs/key_design_points.md) file for details.

## Important Notes

Some of the important notes for understanding this implementation are listed in docs directory, see the [IMPORTANT_NOTES](docs/important_notes.md) file for details.

## Known Issues

* Need to refactor all headers into a single `includes` directory.

## Contributing

We welcome contributions! Please follow these general guidelines:

* Fork the repository.
* Create a new branch for your feature or bug fix.
* Ensure your code adheres to the existing style.
* Write unit tests for new functionality.
* Open a pull request with a clear description of your changes.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
