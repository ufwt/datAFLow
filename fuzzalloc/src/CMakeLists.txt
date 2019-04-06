if(NOT DEFINED AFL_PATH AND DEFINED ENV{AFL_PATH})
  set(AFL_PATH "$ENV{AFL_PATH}")
endif()

if("${AFL_PATH}" STREQUAL "")
  message(FATAL_ERROR "AFL instrumentation enabled but AFL source path not provided."
                      " Provide the AFL source path in the AFL_PATH environment variable")
endif()

get_filename_component(AFL_PATH ${AFL_PATH} ABSOLUTE)

if(EXISTS "${AFL_PATH}/afl-fuzz.c")
  message(STATUS "AFL source code found at ${AFL_PATH}")
else()
  message(FATAL_ERROR "afl-fuzz.c not found in ${AFL_PATH}."
                      " Are you sure that this is the AFL source directory?")
endif()

include_directories(include)
include_directories(${AFL_PATH})

add_subdirectory(llvm)
add_subdirectory(malloc)
add_subdirectory(tools)
