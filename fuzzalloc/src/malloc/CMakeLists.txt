add_library(fuzzalloc SHARED malloc.c
                             deref.c)

if(POOL_SIZE)
  message(STATUS "Allocation pool size set to ${POOL_SIZE} bytes")
  add_definitions(-DPOOL_SIZE=${POOL_SIZE})
endif(POOL_SIZE)

if(AFL_INSTRUMENT)
  if(NOT DEFINED AFL_PATH AND DEFINED ENV{AFL_PATH})
    set(AFL_PATH "$ENV{AFL_PATH}")
  endif()

  if("${AFL_PATH}" STREQUAL "")
    message(FATAL_ERROR "AFL instrumentation enabled but AFL source path not provided."
                        " Provide the AFL source path in the AFL_PATH environment variable")
  endif()

  get_filename_component(AFL_PATH ${AFL_PATH} ABSOLUTE)
  set(AFL_LLVM_RT "${AFL_PATH}/llvm_mode/afl-llvm-rt.o.c")

  if(EXISTS ${AFL_LLVM_RT})
    message(STATUS "AFL source code found at ${AFL_PATH}")
  else()
    message(FATAL_ERROR "${AFL_LLVM_RT} not found in ${AFL_PATH}."
                        " Are you sure that this is the AFL source directory?")
  endif()

  # Need to remove -Werror for afl-llvm-rt.o.c
  string(REPLACE " -Werror" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

  target_sources(fuzzalloc PUBLIC ${AFL_LLVM_RT})
  target_include_directories(fuzzalloc PRIVATE ${AFL_PATH})
  add_definitions(-DAFL_INSTRUMENT)
endif()
