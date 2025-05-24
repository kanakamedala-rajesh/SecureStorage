# The main library target (SecureStorage_lib) should be available from the parent scope
if(NOT TARGET SecureStorage_lib)
    message(FATAL_ERROR "SecureStorage_lib target not found. Ensure it's defined in the parent CMake project.")
endif()

add_executable(SecureStorageExample
    basic_usage.cpp
)

# Link the example against the SecureStorage library
target_link_libraries(SecureStorageExample PRIVATE
    SecureStorage_lib
)

# If your library needs specific compile features or definitions,
# they should ideally be set on SecureStorage_lib with PUBLIC/INTERFACE scope.
# If not, you might need to set them for the example too.
# target_compile_features(SecureStorageExample PRIVATE cxx_std_11) # Should be inherited

install(TARGETS SecureStorageExample DESTINATION bin) # Optional: if you want to install the example