#ifndef SS_FILE_WATCHER_H
#define SS_FILE_WATCHER_H

#include "Error.h"
#include "Logger.h" // For logging within the watcher
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <functional> // For std::function (optional callback)

// Forward declaration for inotify event (Linux specific)
struct inotify_event;

namespace SecureStorage {
namespace FileWatcher {

/**
 * @struct WatchedEvent
 * @brief Structure to hold information about a detected file event.
 */
struct WatchedEvent {
    std::string filePath;      ///< Full path of the file/directory affected
    std::string fileName;      ///< Name of the file/directory within the watched item (if any)
    uint32_t    mask;          ///< The inotify event mask
    bool        isDir;         ///< True if the event is on a directory
    std::string eventNameStr;  ///< Human-readable event name(s)
};

// Alias for a callback function type that the FileWatcher can invoke upon events
using EventCallback = std::function<void(const WatchedEvent& event)>;


/**
 * @class FileWatcher
 * @brief Monitors files and directories for modifications using inotify (Linux-specific).
 *
 * This class runs a dedicated thread to listen for file system events on specified paths.
 * It logs detected events and can optionally invoke a user-provided callback.
 *
 * Currently, it monitors for:
 * - IN_MODIFY: File was modified.
 * - IN_CLOSE_WRITE: File opened for writing was closed.
 * - IN_ATTRIB: Metadata changed (e.g., permissions).
 * - IN_CREATE: File/directory created in watched directory.
 * - IN_DELETE: File/directory deleted from watched directory.
 * - IN_MOVED_FROM / IN_MOVED_TO: File/directory moved.
 */
class FileWatcher {
public:
    /**
     * @brief Constructs a FileWatcher.
     * @param eventLogCallback An optional callback function to be invoked when an event is detected.
     * The watcher will always log events internally.
     */
    explicit FileWatcher(EventCallback eventLogCallback = nullptr);
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;
    FileWatcher(FileWatcher&&) = delete;
    FileWatcher& operator=(FileWatcher&&) = delete;

    /**
     * @brief Starts the file monitoring thread and initializes inotify.
     * @return true if successfully started, false otherwise.
     */
    bool start();

    /**
     * @brief Stops the file monitoring thread and cleans up resources.
     * This method will block until the monitoring thread has joined.
     */
    void stop();

    /**
     * @brief Adds a path (file or directory) to the watch list.
     * If a directory is added, events for files within it (like IN_CREATE, IN_DELETE)
     * and modifications to the directory itself will be monitored.
     *
     * @param path The absolute path to the file or directory to monitor.
     * @return true if the path was successfully added to watch list, false otherwise.
     */
    bool addWatch(const std::string& path);

    /**
     * @brief Removes a path from the watch list.
     * @param path The path to stop monitoring.
     * @return true if successfully removed, false if it was not being watched or an error occurred.
     */
    bool removeWatch(const std::string& path);

private:
    void monitorLoop();
    void processInotifyEvent(const struct inotify_event* event);
    std::string eventMaskToString(uint32_t mask) const;

    int m_inotifyFd;
    int m_pipeFd[2]; // Pipe for signaling the thread to stop

    std::thread m_monitorThread;
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_stoppedByUser;

    std::mutex m_watchMutex; /// Mutex for protecting m_watchDescriptors and m_pathToWd
    std::map<int, std::string> m_wdToPathMap; // Map watch descriptor to path
    std::map<std::string, int> m_pathToWdMap; // Map path to watch descriptor (for removeWatch)

    EventCallback m_eventCallback;
};

} // namespace FileWatcher
} // namespace SecureStorage

#endif // SS_FILE_WATCHER_H