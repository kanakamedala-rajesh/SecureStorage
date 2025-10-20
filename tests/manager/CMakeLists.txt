# Ensure Google Test targets are available
if(NOT TARGET gtest OR NOT TARGET gtest_main)
    message(FATAL_ERROR "Google Test (gtest, gtest_main) targets not found. Ensure FetchContent is working.")
endif()

# Add the executable for SecureStorageManager tests
add_executable(test_ss_manager
    TestSecureStorageManager.cpp
    ../main_test.cpp # Common test runner main
)

# Link against the main SecureStorage library (which includes SecureStorageManager)
# and Google Test.
# SecureStorage_lib itself links to ss_storage, ss_crypto, ss_utils.
target_link_libraries(test_ss_manager PRIVATE
    SecureStorage_lib   # The library under test
    gtest               # Google Test framework
)

# Ensure headers from SecureStorage_lib (like SecureStorage.h) are findable
# target_include_directories for SecureStorage_lib in src/CMakeLists.txt
# with PUBLIC should make its headers available. If not, explicitly add:
# target_include_directories(test_ss_manager PRIVATE
#     "${CMAKE_SOURCE_DIR}/src"
# )

# Discover and add tests to CTest
gtest_discover_tests(test_ss_manager)