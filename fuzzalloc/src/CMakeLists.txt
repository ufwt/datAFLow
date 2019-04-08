if(NOT DEFINED AFL_PATH AND DEFINED ENV{AFL_PATH})
  set(AFL_PATH "$ENV{AFL_PATH}")
else()
  # By default assume that the AFL source is in the root source directory
  set(AFL_PATH "${CMAKE_SOURCE_DIR}/afl-2.52b")
endif()

get_filename_component(AFL_PATH ${AFL_PATH} ABSOLUTE)

if(EXISTS "${AFL_PATH}/afl-fuzz.c")
  message(STATUS "AFL source code found at ${AFL_PATH}")
else()
  message(FATAL_ERROR "afl-fuzz.c not found in ${AFL_PATH}. "
                      "Are you sure that this is the AFL source directory?")
endif()

include_directories(include)
include_directories(${AFL_PATH})

add_subdirectory(lib)
add_subdirectory(runtime)
add_subdirectory(tools)
