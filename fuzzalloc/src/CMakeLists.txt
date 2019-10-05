if(NOT DEFINED AFL_PATH AND DEFINED ENV{AFL_PATH})
  set(AFL_PATH "$ENV{AFL_PATH}")
endif()

set(SVF_PATH "${CMAKE_SOURCE_DIR}/../SVF")

if(NOT DEFINED AFL_PATH)
    message(FATAL_ERROR "AFL source code path not specified."
                        "Set AFL_PATH appropriately.")
endif()

get_filename_component(AFL_PATH ${AFL_PATH} ABSOLUTE)

if(EXISTS "${AFL_PATH}/afl-fuzz.c")
  message(STATUS "AFL source code found at ${AFL_PATH}")
else()
  message(FATAL_ERROR "afl-fuzz.c not found in ${AFL_PATH}. "
                      "Are you sure that this is the AFL source directory? "
                      "Set AFL_PATH appropriately.")
endif()

include_directories(${AFL_PATH})
include_directories(include)

add_subdirectory(llvm-files)
add_subdirectory(runtime)
add_subdirectory(tools)
