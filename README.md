# SecureStorage Library

## System Requirements and Project Proposal

Refer this document for complete details, requirements and specifications followed in this implementation.

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
* **CMake Build System:** Includes `WorkspaceContent` for Mbed TLS dependency.
* **Unit Tested:** Includes a suite of unit tests for core components.

## Build Prerequisites

* CMake (version 3.14 or higher for FetchContent)
* A C++11 compatible compiler (e.g., GCC, Clang)
* Mbed TLS (development headers and libraries). The build system will attempt to download and build Mbed TLS automatically using CMake's `WorkspaceContent` if it's not found on the system.
* Git (for FetchContent)
* (Optional) Doxygen and Graphviz (dot) for generating documentation.
* (Optional) Google Test (development libraries). The build system will attempt to download and build Google Test automatically using CMake's `WorkspaceContent` for running unit tests.

## Build Instructions

1. **Clone the repository (if applicable):**

    ```bash
    git clone <repository_url>
    cd SecureStorage
    ```

2. **Configure with CMake:**

    ```bash
    mkdir build
    cd build
    cmake ..
    ```

    *Mbed TLS and Google Test will be fetched and configured at this stage if not already present.*

3. **Build the library and examples:**

    ```bash
    cmake --build .
    ```

    Or, on Linux/macOS:

    ```bash
    make -j$(nproc)
    ```

4. **(Optional) Run Tests:**

    ```bash
    cd tests
    ctest -V
    ```

    *(Ensure tests are enabled in your CMake configuration if you want to run them).*

5. **(Optional) Generate Documentation:**
    If Doxygen is set up:

    ```bash
    cmake --build . --target doxygen
    ```

    The documentation will typically be generated in `build/docs/doxygen_html/index.html`.

## Continuous Integration & Releases

This project uses GitHub Actions for Continuous Integration (CI). The CI pipeline, defined in `.github/workflows/ci.yml`, automatically performs the following:

* **Builds on Multiple Platforms/Architectures:** Compiles the `SecureStorage` library and its examples on:
    * Linux (latest, GCC, Release)
    * Linux (latest, Clang, Release)
    * Android API 21 (arm64-v8a, using a recent NDK like r25c, Release)
    * Android API 18 (armeabi-v7a, using a recent NDK like r25c, Release). *Note: Modern NDKs have a minimum supported API level (e.g., API 19 for NDK r25c). While targeting API 18 for the manifest is possible, the underlying system headers and libraries used during compilation will correspond to the NDK's minimum (e.g., API 19).*
* **Runs Unit Tests:** Executes the test suite on supported host platforms (Linux).
* **Generates Doxygen Documentation:** Builds the HTML Doxygen documentation.
* **Creates Releases:** When a tag matching the pattern `v*` (e.g., `v1.0.0`, `v1.2.3-beta`) is pushed OR when manually triggered for testing (see section below):
    * Downloads build artifacts (installable packages for each platform/architecture) and Doxygen documentation.
    * Packages these into `.tar.gz` (for Linux and Android builds) and `.zip` (for Doxygen documentation) archives.
    * Creates a GitHub Release with the tag.
    * Uploads the packaged archives as release assets.

The build artifacts (installable packages containing headers, libraries, and potentially examples) and Doxygen documentation are made available with each release on GitHub.

### Testing the Release Process

The GitHub Actions workflow is designed to allow testing of the release and tagging mechanism without making an actual production release. This is achieved using the `workflow_dispatch` trigger.

**Steps to Manually Test a Release:**

1.  **Navigate to Actions:** Go to your repository's "Actions" tab on GitHub.
2.  **Select Workflow:** In the left sidebar, click on the "SecureStorage Build on multiple platforms" workflow.
3.  **Run Workflow:** You will see a "Run workflow" button appear on the right. Click it.
4.  **Input Test Tag (Optional):**
    * A dropdown will appear allowing you to "Run workflow from branch 'main'" (or your selected branch).
    * You'll see an input field labeled "Tag name for test release (e.g., v0.0.0-test). If empty, a default test tag is used."
    * You can enter a custom tag name here for your test release (e.g., `v1.2.3-test-my-feature`).
    * If you leave it blank, it will use a default like `v0.0.0-manual-test-TIMESTAMP`.
5.  **Execute:** Click the green "Run workflow" button.

**What Happens During a Manual Test Release:**

* All the build jobs (Linux, Android, Docs) will run as usual, producing their respective artifacts.
* The `release` job will then proceed:
    * It will use the test tag name you provided (or the default) instead of an actual Git tag from a push.
    * It will create a **Draft Release** on GitHub. This means the release is created but not publicly visible until you explicitly publish it.
    * All build artifacts (Linux tar.gz, Android tar.gz, Doxygen zip) will be packaged and uploaded to this draft release.
    * The release name will be prefixed with "Test Release: ".

**Verification Steps:**

1.  **Monitor Workflow:** Observe the workflow run in the Actions tab. Ensure all build jobs and the release job complete successfully.
2.  **Check Draft Release:**
    * Go to your repository's "Releases" page.
    * You should find a new **draft** release matching the test tag name.
    * Verify that the release notes look correct.
    * Check the assets attached to the draft release. Ensure all expected archives are present and their names correctly reflect the test version.
    * Download and inspect assets to ensure correct packaging.
3.  **Cleanup:** Delete the draft release from the GitHub Releases page after verification.

This manual trigger allows thorough testing of the release automation before pushing an actual `v*` tag for a production release.

## Basic Usage (`SecureStorageManager`)

The primary interface to the library is the `SecureStorage::SecureStorageManager` class.

```cpp
#include "SecureStorage.h" // Main library header
#include <iostream>
#include <vector>
#include <string>

int main() {
    // Configure the library's logger (optional)
    // SecureStorage::Utils::Logger::getInstance().setLogLevel(SecureStorage::Utils::LogLevel::DEBUG);

    std::string app_root_storage = "./app_secure_files";
    std::string device_unique_serial = "MyDevice001"; // Use the actual unique device serial

    SecureStorage::SecureStorageManager manager(app_root_storage, device_unique_serial);

    if (!manager.isInitialized()) {
        std::cerr << "Error: SecureStorageManager failed to initialize." << std::endl;
        return 1;
    }
    std::cout << "SecureStorageManager initialized." << std::endl;
    if (manager.isFileWatcherActive()) {
        std::cout << "File watcher is active on: " << app_root_storage << std::endl;
    } else {
        std::cout << "File watcher is NOT active." << std::endl;
    }

    std::string data_id = "user_settings";
    std::vector<unsigned char> data_to_save = {'s', 'e', 'c', 'r', 'e', 't', ' ', 'd', 'a', 't', 'a'};

    // Store data
    if (manager.storeData(data_id, data_to_save) == SecureStorage::Error::Errc::Success) {
        std::cout << "Data stored successfully for ID: " << data_id << std::endl;
    } else {
        std::cerr << "Failed to store data for ID: " << data_id << std::endl;
    }

    // Retrieve data
    std::vector<unsigned char> retrieved_data;
    if (manager.retrieveData(data_id, retrieved_data) == SecureStorage::Error::Errc::Success) {
        std::cout << "Retrieved data for ID " << data_id << ": ";
        for (unsigned char c : retrieved_data) { std::cout << c; }
        std::cout << std::endl;
    } else {
        std::cerr << "Failed to retrieve data for ID: " << data_id << std::endl;
    }

    // Check data existence
    if (manager.dataExists(data_id)) {
        std::cout << "Data for ID " << data_id << " exists." << std::endl;
    }

    // List IDs
    std::vector<std::string> ids;
    manager.listDataIds(ids);
    std::cout << "Stored IDs: ";
    for (const auto& id_item : ids) { std::cout << id_item << " "; }
    std::cout << std::endl;
    
    // Delete data
    // manager.deleteData(data_id);

    // The manager's destructor will automatically stop the file watcher.
    return 0;
}
```

See also the examples/ directory for a command-line encryption/decryption utility using the library's components.

## API Documentation

Detailed API documentation can be generated using Doxygen. If you have built the `doxygen` target as described in the build instructions, you can find the documentation at `build/docs/doxygen_html/index.html`.

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
*   Fork the repository.
*   Create a new branch for your feature or bug fix.
*   Ensure your code adheres to the existing style.
*   Write unit tests for new functionality.
*   Open a pull request with a clear description of your changes.

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
