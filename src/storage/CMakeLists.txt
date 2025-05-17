add_library(ss_storage STATIC
    SecureStore.cpp
)

# Public include for SecureStore.h
target_include_directories(ss_storage PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Link ss_storage against its dependencies:
# ss_crypto for encryption/decryption and key provision.
# ss_utils for file operations, logging, and error handling.
target_link_libraries(ss_storage PUBLIC
    ss_crypto
    ss_utils
)

target_compile_features(ss_storage PUBLIC cxx_std_11)