# Important Notes

### Important Notes for the Encryptor.cpp implementation:

- GCM Context Management: The mbedtls_gcm_context should ideally be re-initialized (mbedtls_gcm_init) or reset (mbedtls_gcm_setkey effectively does this for the key part) if you are using the same context for multiple independent encryption/decryption operations with different keys or modes. For simplicity here, I've added a mbedtls_gcm_free and mbedtls_gcm_init after each operation. A more performant approach for multiple operations with the same key would be to call mbedtls_gcm_setkey once, then mbedtls_gcm_starts (or mbedtls_gcm_crypt_and_tag / mbedtls_gcm_auth_decrypt which handle starts internally for single-shot ops) multiple times with different IVs. Since our encrypt and decrypt methods are self-contained and take the key each time, re-initializing or relying on setkey to reset is fine.

- RNG Personalization: The personalization string for mbedtls_ctr_drbg_seed is important. Using a fixed string like "SecureStorageEncryptorSeed" is okay, but for enhanced security in some scenarios, appending device-unique data (like part of the serial number, if not sensitive itself) can be beneficial.

- Error Handling in Encryptor::Impl Constructor: If mbedtls_ctr_drbg_seed fails, the Encryptor is left in a bad state. The current code logs this. In a production system, this might warrant throwing an exception during construction or having an isValid() method that the caller must check. My Error::Errc Encryptor::encrypt/decrypt methods now check m_impl->initialized.


### Important Notes for FileUtil.cpp:

- **Atomic Write Robustness (`atomicWriteFile`):**
    - To ensure data integrity against power loss, `atomicWriteFile` now employs a more robust strategy on POSIX-compliant systems (like Linux, Android NDK targets):
        1. Data is written to a temporary file using C-style file I/O (`open`, `write`).
        2. `fsync()` is called on the temporary file's descriptor before closing it to ensure its contents are physically written to disk.
        3. The temporary file is then atomically renamed to the target file path.
        4. `fsync()` is called on the parent directory of the target file. This is critical to ensure that the directory entry changes from the `rename` operation are also persisted to disk, protecting against scenarios where the file data is written but the file system metadata isn't updated before a crash.
    - This multi-step process, particularly the `fsync` on both the file and its directory, significantly enhances data resilience compared to relying solely on `std::ofstream::close()`.
    - On Windows, a full equivalent (especially for directory `fsync`) is handled differently by the OS; the current POSIX-focused enhancement provides the strongest guarantees on the target Linux-based platforms.

- createDirectories: The C++11 implementation is manual. C++17 std::filesystem::create_directories would simplify this significantly. The provided version iterates and creates directory components one by one. It assumes POSIX mkdir. Permissions are set to 0755 by default on POSIX.

- Error Handling: strerror(errno) is used for system call error details.

- Directory Listing: Uses POSIX opendir, readdir, closedir. It only lists regular files in the immediate directory by default.
