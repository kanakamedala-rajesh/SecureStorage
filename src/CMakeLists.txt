# Add the utils subdirectory first, as other components might depend on it
add_subdirectory(utils)
add_subdirectory(crypto)
add_subdirectory(storage)
add_subdirectory(file_watcher)

# Define the main library
# This could be an INTERFACE library if it only contains headers,
# or a STATIC/SHARED library if it has its own .cpp files.
# For now, since it will include some cpp files, let's make it a static library.
add_library(SecureStorage_lib STATIC
    SecureStorageManager.cpp
    # Add .cpp files here as they are created
)

# Public include directories for the library
# This makes headers in src/ (like SecureStorage.h) available
target_include_directories(SecureStorage_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" # For public headers directly in src/
    # Add other public include directories if needed
    # "${CMAKE_CURRENT_SOURCE_DIR}/include" # Example for a separate include directory
)

# Link dependencies
# All components of our library will need storage
target_link_libraries(SecureStorage_lib PUBLIC 
    ss_storage
    ss_file_watcher
    )

# Mbed TLS libraries: mbedcrypto is the core.
# mbedtls provides TLS functionalities, mbedx509 provides X.509 certificate parsing.
# We primarily need mbedcrypto for AES, GCM, SHA, HKDF.
target_link_libraries(SecureStorage_lib PUBLIC mbedcrypto)
# If you end up using features from mbedtls (full TLS stack) or mbedx509, add them here.

# Set properties for the library if needed, e.g., C++ standard
target_compile_features(SecureStorage_lib PUBLIC cxx_std_11)

# If you have specific .cpp files for SecureStorage_lib directly under src/
# target_sources(SecureStorage_lib PRIVATE
#    SecureStorageManager.cpp # Example
# )