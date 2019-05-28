add_custom_target(libptmalloc3
                  COMMAND make linux-shared
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_custom_command(TARGET libptmalloc3
                   POST_BUILD
                   COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/libptmalloc3.so ${CMAKE_CURRENT_BINARY_DIR})
