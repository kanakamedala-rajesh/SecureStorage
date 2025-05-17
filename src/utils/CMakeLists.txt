add_library(ss_utils STATIC
    Logger.cpp
    Error.cpp
    FileUtil.cpp
)

target_include_directories(ss_utils PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" # Makes Logger.h and Error.h available
)

# Mbed TLS might not be directly needed by utils, but if it were:
# target_link_libraries(ss_utils PUBLIC mbedcrypto)

target_compile_features(ss_utils PUBLIC cxx_std_11)