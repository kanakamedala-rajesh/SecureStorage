add_executable(test_ss_crypto
    test_KeyProvider.cpp
    test_Encryptor.cpp
    ../main_test.cpp # Common test runner main
)

# Link against the crypto library we are testing (ss_crypto),
# its dependency ss_utils (for logger, error in tests if needed directly),
# Mbed TLS (mbedcrypto for any direct test usage, though mostly covered by ss_crypto),
# and Google Test.
target_link_libraries(test_ss_crypto PRIVATE
    ss_crypto       # The library under test
    ss_utils        # Dependency of ss_crypto, also for test logging
    mbedcrypto      # Dependency of ss_crypto
    gtest           # Google Test framework
    gtest_main      # Google Test main
)

add_test(NAME SsCryptoTests COMMAND test_ss_crypto)