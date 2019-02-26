if(AFL_INSTRUMENT)
  if(NOT DEFINED AFL_PATH AND DEFINED ENV{AFL_PATH})
    set(AFL_PATH "$ENV{AFL_PATH}")
  endif()

  if("${AFL_PATH}" STREQUAL "")
    message(FATAL_ERROR "AFL instrumentation enabled but AFL source path not provided."
                        " Provide the AFL source path in the AFL_PATH environment variable")
  endif()

  get_filename_component(AFL_PATH "${AFL_PATH}" ABSOLUTE)
  set(AFL_LLVM_RT "${AFL_PATH}/llvm_mode/afl-llvm-rt.o.c")

  if(EXISTS ${AFL_LLVM_RT})
    message(STATUS "AFL source code found at ${AFL_PATH}")
  else()
    message(FATAL_ERROR "${AFL_LLVM_RT} not found in ${AFL_PATH}."
                        " Are you sure that this is the AFL source directory?")
  endif()

  # Need to remove -Werror when using AFL source code
  string(REPLACE " -Werror" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

  # Get some directory paths to pass to dataflow-clang-fast
  get_filename_component(FUZZALLOC_LLVM_DIR
                         "${CMAKE_CURRENT_BINARY_DIR}/../llvm/Transforms"
                         ABSOLUTE)
  get_filename_component(FUZZALLOC_MALLOC_DIR
                         "${CMAKE_CURRENT_BINARY_DIR}/../malloc"
                         ABSOLUTE)

  add_definitions(-DFUZZALLOC_LLVM_DIR="${FUZZALLOC_LLVM_DIR}"
                  -DFUZZALLOC_MALLOC_DIR="${FUZZALLOC_MALLOC_DIR}")

  add_executable(dataflow-clang-fast dataflow-clang-fast.c)
  target_include_directories(dataflow-clang-fast PRIVATE ${AFL_PATH})

  add_executable(dataflow-clang-fast++ dataflow-clang-fast.c)
  target_include_directories(dataflow-clang-fast++ PRIVATE ${AFL_PATH})
endif()
