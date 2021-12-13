if(NOT DEFINED AFLPP_DIR AND DEFINED ENV{AFLPP_DIR})
  set(AFLPP_DIR "$ENV{AFLPP_DIR}")
endif()

set(SVF_PATH "${CMAKE_SOURCE_DIR}/../SVF")

if(NOT DEFINED AFLPP_DIR)
    message(FATAL_ERROR "AFL++ source code path not specified. "
                        "Set AFLPP_DIR appropriately.")
endif()

get_filename_component(AFLPP_DIR ${AFLPP_DIR} ABSOLUTE)

if(EXISTS "${AFLPP_DIR}/src/afl-fuzz.c")
  message(STATUS "AFL++ source code found at ${AFLPP_DIR}")
else()
  message(FATAL_ERROR "afl-fuzz.c not found in ${AFLPP_DIR}. "
                      "Are you sure that this is the AFL++ source directory? "
                      "Set AFLPP_DIR appropriately.")
endif()

include_directories("${AFLPP_DIR}/include")
include_directories(include)

add_subdirectory(llvm-files)
add_subdirectory(runtime)
add_subdirectory(tools)
