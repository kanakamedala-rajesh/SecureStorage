find_package(Threads REQUIRED)

add_library(ss_file_watcher STATIC
    FileWatcher.cpp
)

# Add _GNU_SOURCE to expose inotify_init1 for older Android API levels
target_compile_definitions(ss_file_watcher PRIVATE _GNU_SOURCE)

# Public include for FileWatcher.h
target_include_directories(ss_file_watcher PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Link ss_file_watcher against its dependencies:
# ss_utils for logging and error handling.
# Needs thread support (pthread on Linux)
target_link_libraries(ss_file_watcher PUBLIC
    ss_utils
    Threads::Threads # CMake's way to link pthreads
)

target_compile_features(ss_file_watcher PUBLIC cxx_std_11)

# Install public headers for ss_file_watcher
install(FILES
    FileWatcher.h
    DESTINATION include/file_watcher # Installs to <prefix>/include/file_watcher
)