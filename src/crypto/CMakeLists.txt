add_library(ss_crypto STATIC
    KeyProvider.cpp
    Encryptor.cpp
)

target_include_directories(ss_crypto PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" # To make KeyProvider.h and Encryptor.h available
)

# Link ss_crypto against ss_utils (for Logger, Error)
# and Mbed TLS's mbedcrypto library.
target_link_libraries(ss_crypto PUBLIC
    ss_utils
    mbedcrypto # Provided by FetchContent for Mbed TLS
    # mbedtls  # Only if TLS-specific features are used directly
    # mbedx509 # Only if X.509 features are used directly
)

target_compile_features(ss_crypto PUBLIC cxx_std_11)