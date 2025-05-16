if(NOT TARGET gtest)
    message(FATAL_ERROR "gtest target not found. Ensure FetchContent for googletest is working.")
endif()
if(NOT TARGET gtest_main)
    message(FATAL_ERROR "gtest_main target not found. Ensure FetchContent for googletest is working.")
endif()


add_executable(test_ss_utils
    test_Logger.cpp
    # Add other test_*.cpp files for utils here
    ../main_test.cpp # Link with the common test main
)

# Link against the utility library we are testing (ss_utils)
# and Google Test
target_link_libraries(test_ss_utils PRIVATE
    ss_utils
    gtest # Provided by FetchContent for googletest
    gtest_main # Provided by FetchContent for googletest
)

# Add this test executable to CTest
add_test(NAME ssUtilsTests COMMAND test_ss_utils)