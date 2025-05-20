# Ensure Google Test targets are available
if(NOT TARGET gtest OR NOT TARGET gtest_main)
    message(FATAL_ERROR "Google Test (gtest, gtest_main) targets not found. Ensure FetchContent is working.")
endif()

find_package(Threads REQUIRED)

# Add the executable for FileWatcher tests
add_executable(test_ss_file_watcher
    TestFileWatcher.cpp
    ../main_test.cpp # Common test runner main
)

# Link against the file_watcher library, its dependencies, and Google Test
target_link_libraries(test_ss_file_watcher PRIVATE
    ss_file_watcher   # The library under test
    ss_utils          # For FileUtil and Logger (used by watcher and potentially by tests)
    Threads::Threads  # For pthread, which FileWatcher uses
    gtest
    gtest_main
)

# Add this test executable to CTest
add_test(NAME SsFileWatcherTests COMMAND test_ss_file_watcher)