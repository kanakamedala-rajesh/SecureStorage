# Enable testing for this project (if not already enabled globally)
enable_testing()

# Add the subdirectory for utility tests
add_subdirectory(utils)
add_subdirectory(crypto)
add_subdirectory(storage)
add_subdirectory(manager)
add_subdirectory(file_watcher)

# You can add more subdirectories for other modules' tests here
# add_subdirectory(crypto)
# add_subdirectory(storage)
# add_subdirectory(file_watcher)