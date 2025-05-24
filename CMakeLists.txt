cmake_minimum_required(VERSION 3.14)
project(SecureStorage LANGUAGES C CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

enable_language(C)

# Mbed TLS specific CMake variables before FetchContent_Declare
# Disable programs and tests from Mbed TLS build as we only need the library
set(ENABLE_PROGRAMS OFF CACHE BOOL "Disable Mbed TLS programs" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "Disable Mbed TLS tests" FORCE) # Mbed TLS's own tests
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "Build Mbed TLS as a static library" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "Build Mbed TLS as a static library" FORCE)

# For Mbed TLS versions that might not use 'main' as default branch
# Check Mbed TLS repository for the correct tag/branch for stable releases.
# Using a specific commit hash or tag is recommended for reproducibility.
# For example, Mbed TLS 3.6.0
set(MBEDTLS_GIT_REPOSITORY "https://github.com/Mbed-TLS/mbedtls.git")
set(MBEDTLS_GIT_TAG "mbedtls-3.6.0") # Use a specific stable release tag

FetchContent_Declare(
    mbedtls
    GIT_REPOSITORY ${MBEDTLS_GIT_REPOSITORY}
    GIT_TAG        ${MBEDTLS_GIT_TAG}
    GIT_PROGRESS   TRUE
    # GIT_SHALLOW TRUE # Potentially faster clone, but might cause issues with some Mbed TLS build scripts
)

# Make mbedtls available. This will download and configure it.
# It creates targets like mbedcrypto, mbedx509, mbedtls
FetchContent_MakeAvailable(mbedtls)

# --- Google Test Dependency (for unit testing) ---
set(GTEST_GIT_REPOSITORY "https://github.com/google/googletest.git")
# Use a specific release tag for googletest
set(GTEST_GIT_TAG "release-1.12.1") # Example: Use a recent stable release

FetchContent_Declare(
  googletest
  GIT_REPOSITORY ${GTEST_GIT_REPOSITORY}
  GIT_TAG        ${GTEST_GIT_TAG}
  GIT_PROGRESS   TRUE
)

# For googletest, we need to set this BEFORE FetchContent_MakeAvailable
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) # If using MSVC, to avoid linkage issues
set(BUILD_GMOCK OFF CACHE BOOL "Disable building gmock" FORCE) # We only need gtest
set(INSTALL_GTEST OFF CACHE BOOL "Disable installing gtest" FORCE)

FetchContent_MakeAvailable(googletest)


# --- Project Structure ---
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(examples)

# --- Installation (Optional, implemented later) ---
# include(GNUInstallDirs)
# install(TARGETS SecureStorage_lib # Assuming your library target is named this
#     EXPORT SecureStorageTargets
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} # For shared libs on Windows
#     INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/SecureStorage
# )
# install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/ # Or specific public headers
#     DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/SecureStorage
#     FILES_MATCHING PATTERN "*.h"
# )

# install(EXPORT SecureStorageTargets
#   FILE SecureStorageTargets.cmake
#   NAMESPACE SecureStorage::
#   DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SecureStorage
# )

# --- Doxygen (Optional, can be added later) ---
# find_package(Doxygen)
# if(DOXYGEN_FOUND)
#     set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
#     set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
#     configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
#     add_custom_target(doxygen ALL
#         COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
#         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#         COMMENT "Generating API documentation with Doxygen"
#         VERBATIM)
# else()
#     message(STATUS "Doxygen not found. Documentation will not be generated.")
# endif()

message(STATUS "Mbed TLS source dir: ${mbedtls_SOURCE_DIR}")
message(STATUS "Mbed TLS binary dir: ${mbedtls_BINARY_DIR}")
message(STATUS "Google Test source dir: ${googletest_SOURCE_DIR}")
message(STATUS "Google Test binary dir: ${googletest_BINARY_DIR}")