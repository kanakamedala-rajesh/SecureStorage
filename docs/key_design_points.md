### Key Design Points in SecureStore.cpp:

- Initialization (SecureStore constructor):
    - Validates paths.
    - Creates the root storage directory.
    - Initializes KeyProvider and Encryptor.
    - Derives and stores a m_masterKey. If any of these steps fail, m_initialized remains false, and operations will fail early.

- validateDataId: A crucial (though simple here) method to prevent data_id from being misused for path traversal or creating invalid filenames. Production systems might need more robust validation (e.g., regex for allowed characters).

- storeData Backup Strategy:
    - storeData Backup Strategy:
    - Encrypts data.
    - Writes to a temporary file (e.g., id.enc.tmp) using `FileUtil::atomicWriteFile`. `FileUtil::atomicWriteFile` implements a robust write-fsync-rename-fsync_dir strategy to ensure the data written to this temporary file is durable.
    - If a main_file (e.g., id.enc) exists:
        - Delete old backup_file (e.g., id.enc.bak) if it exists.
        - Rename main_file to backup_file.
    - Rename the temporary file (now id.enc.tmp) to main_file.
    - This ensures that there's always either a valid main_file or a backup_file (or both) if the operation is interrupted.

- retrieveData Resilience:
    - Tries to read and decrypt the main_file.
    - If that fails (read error, file not found, decryption/authentication error), it tries the backup_file.
    - If data is successfully retrieved from the backup_file, it attempts to restore this (still encrypted) data back to the main_file location using FileUtil::atomicWriteFile. This "heals" the main file.

- File Naming: Uses consistent extensions (.enc, .bak, .tmp).

- Error Handling: Propagates errors using Error::Errc and logs extensively using SS_LOG_* macros.



### Key Features of this FileWatcher Implementation:

- inotify Based: Uses Linux inotify for efficient file system event monitoring.

- Threaded: Runs monitorLoop in a separate std::thread to avoid blocking the caller.

- Stop Mechanism: Uses a pipe to signal the monitoring thread to terminate gracefully. poll() is used to wait on both the inotify FD and the pipe FD.

- Event Logging: Logs detailed information about detected events using SS_LOG_INFO.
Specific Events: Configured to watch for a comprehensive set of events including modifications, closes after write, attribute changes, creations, deletions, and moves.

- Watch Management: addWatch and removeWatch allow dynamic management of watched paths. Maps m_wdToPathMap and m_pathToWdMap are used to associate watch descriptors with paths.

- Mutex Protected: Access to the watch descriptor maps is protected by m_watchMutex for thread safety between the monitor thread and public methods like addWatch/removeWatch.

- Optional Callback: Supports an optional EventCallback for users to react to events programmatically.

- Error Handling: Uses strerror(errno) for system call errors and logs appropriately.

- Non-Blocking: inotify_init() is used, and the returned file descriptor is set to non-blocking using fcntl(). This, along with a pipe and poll(), manages waiting and is essential for the stop mechanism to work without forced thread termination.
