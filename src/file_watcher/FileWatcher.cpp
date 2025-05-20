#include "FileWatcher.h"
#include "FileUtil.h" // For pathExists, although stat is used here

#include <sys/inotify.h> // For inotify_init1, inotify_add_watch, struct inotify_event
#include <unistd.h>      // For read, close, pipe
#include <cerrno>        // For errno
#include <cstring>       // For strerror, memset
#include <poll.h>        // For poll() to handle inotify FD and pipe FD
#include <climits>       // For NAME_MAX

// Define event buffer size (can be tuned)
#define EVENT_BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))
// NAME_MAX is often 255 on Linux

namespace SecureStorage {
namespace FileWatcher {

FileWatcher::FileWatcher(EventCallback eventLogCallback)
    : m_inotifyFd(-1),
      m_isRunning(false),
      m_stoppedByUser(false),
      m_eventCallback(std::move(eventLogCallback)) {
    m_pipeFd[0] = -1;
    m_pipeFd[1] = -1;
}

FileWatcher::~FileWatcher() {
    stop(); // Ensure thread is stopped and resources are cleaned up
}

bool FileWatcher::start() {
    if (m_stoppedByUser.load()) { // If stop() was called and completed
        SS_LOG_WARN("FileWatcher: Attempted to start a watcher that has been definitively stopped. Create a new instance to monitor again.");
        return false;
    }

    bool expected_is_running = false;
    // Attempt to set m_isRunning to true only if it's currently false.
    // This handles both initial start and prevents concurrent start calls from re-initializing.
    if (!m_isRunning.compare_exchange_strong(expected_is_running, true)) {
        // If m_isRunning was already true (either running or in process of starting by another thread)
        SS_LOG_WARN("FileWatcher: Start called but watcher is already running or being started.");
        return true; // Indicate it's (or will be) running
    }
    // At this point, m_isRunning is true, and we are responsible for initialization.

    m_inotifyFd = inotify_init1(IN_NONBLOCK);
    if (m_inotifyFd < 0) {
        SS_LOG_ERROR("FileWatcher: Failed to initialize inotify: " << strerror(errno));
        m_isRunning.store(false);
        return false;
    }

    if (pipe(m_pipeFd) < 0) {
        SS_LOG_ERROR("FileWatcher: Failed to create pipe: " << strerror(errno));
        close(m_inotifyFd);
        m_inotifyFd = -1;
        m_isRunning.store(false);
        return false;
    }

    try {
        m_monitorThread = std::thread(&FileWatcher::monitorLoop, this);
    } catch (const std::system_error& e) {
        SS_LOG_ERROR("FileWatcher: Failed to start monitor thread: " << e.what());
        close(m_inotifyFd);
        m_inotifyFd = -1;
        close(m_pipeFd[0]);
        close(m_pipeFd[1]);
        m_pipeFd[0] = m_pipeFd[1] = -1;
        m_isRunning.store(false);
        return false;
    }

    SS_LOG_INFO("FileWatcher: Started successfully.");
    return true;
}

void FileWatcher::stop() {
    bool expected_running = true;
    // Try to set m_isRunning from true to false. If it was already false,
    // it means either it was never started, started & failed, or stop() was already called.
    if (!m_isRunning.compare_exchange_strong(expected_running, false)) {
        SS_LOG_INFO("FileWatcher: Stop called, but watcher was not in a fully running state (m_isRunning was false).");
        // If m_stoppedByUser is already true, we've been through full stop before.
        if (m_stoppedByUser.load()) {
             SS_LOG_DEBUG("FileWatcher: Already fully stopped and cleaned up.");
             return;
        }
        // If thread is joinable, it means it might have started but exited prematurely or is stuck
        // while m_isRunning became false due to an error.
        if (m_monitorThread.joinable()) {
            SS_LOG_DEBUG("FileWatcher: Attempting to join lingering thread.");
            if (m_pipeFd[1] != -1) { // Signal just in case
                char dummy = 'S';
                if (write(m_pipeFd[1], &dummy, 1) < 0 && errno != EPIPE) {
                     SS_LOG_WARN("FileWatcher::stop - Error writing to pipe for lingering thread: " << strerror(errno));
                }
            }
            m_monitorThread.join();
        }
    } else {
        // m_isRunning was true and successfully set to false. This is the normal stop path.
        SS_LOG_INFO("FileWatcher: Stopping monitor thread...");
        if (m_pipeFd[1] != -1) {
            char dummy = 'S'; // Signal to stop
            if (write(m_pipeFd[1], &dummy, 1) < 0 && errno != EPIPE) { // EPIPE is ok if read end already closed
                SS_LOG_ERROR("FileWatcher: Error writing to pipe to signal stop: " << strerror(errno));
            }
        }
        if (m_monitorThread.joinable()) {
            m_monitorThread.join();
        }
        SS_LOG_INFO("FileWatcher: Monitor thread joined.");
    }

    // Common cleanup for FDs and maps
    { // Scope for lock
        std::lock_guard<std::mutex> lock(m_watchMutex);
        if (m_inotifyFd != -1) {
            for (const auto& pair : m_wdToPathMap) {
                inotify_rm_watch(m_inotifyFd, pair.first);
            }
            m_wdToPathMap.clear();
            m_pathToWdMap.clear();
            close(m_inotifyFd);
            m_inotifyFd = -1;
            SS_LOG_DEBUG("FileWatcher: Inotify FD closed and watches removed.");
        }
    } // Mutex released

    if (m_pipeFd[0] != -1) { close(m_pipeFd[0]); m_pipeFd[0] = -1; }
    if (m_pipeFd[1] != -1) { close(m_pipeFd[1]); m_pipeFd[1] = -1; }
    SS_LOG_DEBUG("FileWatcher: Pipe FDs closed.");

    m_stoppedByUser.store(true); // Mark that stop() has completed its work.
    SS_LOG_INFO("FileWatcher: Stopped and resources cleaned.");
}

bool FileWatcher::addWatch(const std::string& path) {
    if (!m_isRunning.load() || m_inotifyFd < 0) {
        SS_LOG_ERROR("FileWatcher: Not running or inotify not initialized. Cannot add watch for " << path);
        return false;
    }
    if (path.empty()) {
        SS_LOG_ERROR("FileWatcher: Path to watch cannot be empty.");
        return false;
    }
    // For automotive linux, we usually have permissions. Add check if needed.
    if (!Utils::FileUtil::pathExists(path)) {
        SS_LOG_ERROR("FileWatcher: Path does not exist, cannot watch: " << path);
        return false;
    }


    // Define the events we are interested in
    // IN_MODIFY: File was modified.
    // IN_CLOSE_WRITE: File opened for writing was closed.
    // IN_ATTRIB: Metadata changed.
    // IN_CREATE: File/directory created in watched directory.
    // IN_DELETE: File/directory deleted from watched directory.
    // IN_MOVED_FROM / IN_MOVED_TO: File/directory moved.
    // IN_DELETE_SELF / IN_MOVE_SELF: Watched item itself deleted/moved.
    uint32_t mask = IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB |
                      IN_CREATE | IN_DELETE |
                      IN_MOVED_FROM | IN_MOVED_TO |
                      IN_DELETE_SELF | IN_MOVE_SELF;

    std::lock_guard<std::mutex> lock(m_watchMutex);
    if (m_pathToWdMap.count(path)) {
        SS_LOG_WARN("FileWatcher: Path " << path << " is already being watched.");
        return true; // Or false, depending on desired behavior for re-adding
    }

    int wd = inotify_add_watch(m_inotifyFd, path.c_str(), mask);
    if (wd < 0) {
        SS_LOG_ERROR("FileWatcher: Failed to add watch for " << path << ": " << strerror(errno));
        return false;
    }

    m_wdToPathMap[wd] = path;
    m_pathToWdMap[path] = wd;
    SS_LOG_INFO("FileWatcher: Added watch for path: " << path << " (wd: " << wd << ")");
    return true;
}

bool FileWatcher::removeWatch(const std::string& path) {
    if (!m_isRunning.load() || m_inotifyFd < 0) {
        SS_LOG_ERROR("FileWatcher: Not running or inotify not initialized. Cannot remove watch for " << path);
        return false;
    }
     if (path.empty()) {
        SS_LOG_ERROR("FileWatcher: Path to remove watch from cannot be empty.");
        return false;
    }

    std::lock_guard<std::mutex> lock(m_watchMutex);
    auto it = m_pathToWdMap.find(path);
    if (it == m_pathToWdMap.end()) {
        SS_LOG_WARN("FileWatcher: Path " << path << " was not being watched.");
        return false;
    }

    int wd = it->second;
    if (inotify_rm_watch(m_inotifyFd, wd) < 0) {
        SS_LOG_ERROR("FileWatcher: Failed to remove watch for " << path << " (wd: " << wd << "): " << strerror(errno));
        // Descriptor might still be in maps, but inotify failed.
        // Consider what to do here - for now, we still remove from maps.
    }

    m_wdToPathMap.erase(wd);
    m_pathToWdMap.erase(it);
    SS_LOG_INFO("FileWatcher: Removed watch for path: " << path << " (wd: " << wd << ")");
    return true;
}


void FileWatcher::monitorLoop() {
    char buffer[EVENT_BUF_LEN];
    struct pollfd fds[2];

    // Inotify file descriptor
    fds[0].fd = m_inotifyFd;
    fds[0].events = POLLIN;

    // Pipe read end for stop signal
    fds[1].fd = m_pipeFd[0];
    fds[1].events = POLLIN;

    SS_LOG_INFO("FileWatcher: Monitor thread started.");

    while (m_isRunning.load()) {
        // Poll for events on inotify fd or data on pipe fd
        // Timeout of -1 means block indefinitely. Use a timeout if periodic checks are needed.
        int poll_ret = poll(fds, 2, -1); // Block indefinitely until event or signal

        if (!m_isRunning.load()) { // Check immediately after poll returns
            break;
        }

        if (poll_ret < 0) {
            if (errno == EINTR) continue; // Interrupted by a signal, continue if still running
            SS_LOG_ERROR("FileWatcher: poll() failed: " << strerror(errno));
            m_isRunning.store(false); // Critical error, stop the loop
            break;
        }

        // Check if pipe has data (stop signal)
        if (fds[1].revents & POLLIN) {
            SS_LOG_INFO("FileWatcher: Stop signal received on pipe.");
            char dummy_buf[16];
            read(m_pipeFd[0], dummy_buf, sizeof(dummy_buf)); // Drain the pipe
            break; // Exit loop
        }
        
        // Check for inotify events
        if (fds[0].revents & POLLIN) {
            ssize_t len = read(m_inotifyFd, buffer, EVENT_BUF_LEN);
            if (len < 0) {
                if (errno == EINTR) continue; // Interrupted
                if (errno == EAGAIN || errno == EWOULDBLOCK) { // No data available (since non-blocking)
                    // This shouldn't happen if poll indicated POLLIN, but handle defensively
                    continue; 
                }
                SS_LOG_ERROR("FileWatcher: read() from inotify failed: " << strerror(errno));
                m_isRunning.store(false); // Critical error
                break;
            }
            if (len == 0) { // Should not happen with blocking read, but check
                 SS_LOG_WARN("FileWatcher: read() from inotify returned 0 bytes.");
                 continue;
            }

            ssize_t i = 0;
            while (i < len) {
                struct inotify_event *event = reinterpret_cast<struct inotify_event*>(&buffer[i]);
                if (event->wd == -1 && (event->mask & IN_Q_OVERFLOW)) {
                    SS_LOG_WARN("FileWatcher: Inotify event queue overflowed!");
                } else {
                    processInotifyEvent(event);
                }
                i += sizeof(struct inotify_event) + event->len;
            }
        }
    }
    SS_LOG_INFO("FileWatcher: Monitor thread finished.");
}

void FileWatcher::processInotifyEvent(const struct inotify_event* event) {
    std::string path_watched;
    {
        std::lock_guard<std::mutex> lock(m_watchMutex); // Protect map access
        auto it = m_wdToPathMap.find(event->wd);
        if (it == m_wdToPathMap.end()) {
            // This can happen if a watch was removed just before processing its event
            // or if IN_IGNORED is processed after map removal.
            SS_LOG_WARN("FileWatcher: Event for unknown watch descriptor: " << event->wd);
            return;
        }
        path_watched = it->second;
    }

    WatchedEvent watchedEvent;
    watchedEvent.filePath = path_watched;
    watchedEvent.mask = event->mask;
    watchedEvent.isDir = (event->mask & IN_ISDIR);
    if (event->len > 0) {
        watchedEvent.fileName = event->name; // Name of file/dir within watched dir
    }
    watchedEvent.eventNameStr = eventMaskToString(event->mask);

    std::string fullItemPath = path_watched;
    if (!watchedEvent.fileName.empty() && path_watched.back() != '/') {
        fullItemPath += "/";
    }
    fullItemPath += watchedEvent.fileName;


    // Log every operation on these files
    // Customize logging based on which events are considered "unintended writes"
    // For now, log all captured significant events.
    // IN_CLOSE_WRITE is particularly interesting for "unintended writes".
    SS_LOG_INFO("FileWatcher Event: Path='" << path_watched 
                << (watchedEvent.fileName.empty() ? "" : "/"+watchedEvent.fileName)
                << "' FullItemPath='" << fullItemPath
                << "' Mask=0x" << std::hex << event->mask << std::dec
                << " Event(s): [" << watchedEvent.eventNameStr << "]"
                << (watchedEvent.isDir ? " (Directory)" : " (File)"));


    // Handle specific events like watch removal
    if (event->mask & IN_IGNORED) {
        SS_LOG_INFO("FileWatcher: Watch for '" << path_watched << "' (wd: " << event->wd << ") was removed (IN_IGNORED).");
        std::lock_guard<std::mutex> lock(m_watchMutex);
        m_wdToPathMap.erase(event->wd);
        m_pathToWdMap.erase(path_watched); // path_watched might be empty if wd not found, but we checked earlier
    }
    if (event->mask & IN_DELETE_SELF || event->mask & IN_MOVE_SELF) {
         SS_LOG_INFO("FileWatcher: Watched item '" << path_watched << "' itself was deleted or moved.");
        // The watch is automatically removed by the kernel, IN_IGNORED will follow.
    }


    if (m_eventCallback) {
        m_eventCallback(watchedEvent);
    }
}

std::string FileWatcher::eventMaskToString(uint32_t mask) const {
    std::string res;
    if (mask & IN_ACCESS)        res += "ACCESS ";
    if (mask & IN_MODIFY)        res += "MODIFY ";
    if (mask & IN_ATTRIB)        res += "ATTRIB ";
    if (mask & IN_CLOSE_WRITE)   res += "CLOSE_WRITE ";
    if (mask & IN_CLOSE_NOWRITE) res += "CLOSE_NOWRITE ";
    if (mask & IN_OPEN)          res += "OPEN ";
    if (mask & IN_MOVED_FROM)    res += "MOVED_FROM ";
    if (mask & IN_MOVED_TO)      res += "MOVED_TO ";
    if (mask & IN_CREATE)        res += "CREATE ";
    if (mask & IN_DELETE)        res += "DELETE ";
    if (mask & IN_DELETE_SELF)   res += "DELETE_SELF ";
    if (mask & IN_MOVE_SELF)     res += "MOVE_SELF ";
    if (mask & IN_UNMOUNT)       res += "UNMOUNT ";
    if (mask & IN_Q_OVERFLOW)    res += "Q_OVERFLOW ";
    if (mask & IN_IGNORED)       res += "IGNORED ";
    if (mask & IN_ISDIR)         res += "ISDIR ";

    if (!res.empty()) { // Trim trailing space
        res.pop_back();
    }
    return res;
}


} // namespace FileWatcher
} // namespace SecureStorage