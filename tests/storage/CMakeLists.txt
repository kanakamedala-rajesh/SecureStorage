# Ensure Google Test targets are available (defined in top-level CMakeLists.txt)
if(NOT TARGET gtest OR NOT TARGET gtest_main)
    message(FATAL_ERROR "Google Test (gtest, gtest_main) targets not found. Ensure FetchContent is working.")
endif()

# Add the executable for SecureStore tests
add_executable(test_ss_storage
    test_SecureStore.cpp
    ../main_test.cpp # Common test runner main, defined in tests/CMakeLists.txt
)

# Link against the storage library we are testing (ss_storage)
# and its dependencies (ss_crypto, ss_utils), plus Google Test.
target_link_libraries(test_ss_storage PRIVATE
    ss_storage      # The library under test
    ss_crypto       # Dependency of ss_storage
    ss_utils        # Dependency of ss_storage and for test utilities
    gtest           # Google Test framework
    gtest_main      # Google Test main
)

# Add this test executable to CTest for automated test runs
add_test(NAME SsStorageTests COMMAND test_ss_storage)