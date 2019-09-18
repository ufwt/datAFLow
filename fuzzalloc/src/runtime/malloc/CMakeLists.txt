add_library(fuzzalloc SHARED malloc.c
                             deref.c
                             debug.c)
add_dependencies(fuzzalloc ptmalloc3)

if(FUZZALLOC_USE_LOCKS)
  add_definitions(-DFUZZALLOC_USE_LOCKS)
endif()

add_definitions(-DMSPACES)

if(AFL_INSTRUMENT)
  message(STATUS "Building with AFL instrumentation")

  add_definitions(-DAFL_INSTRUMENT)

  # Need to remove -Werror for afl-llvm-rt.o.c
  string(REPLACE " -Werror" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

  target_sources(fuzzalloc PUBLIC "${AFL_PATH}/llvm_mode/afl-llvm-rt.o.c")
endif()

target_include_directories(fuzzalloc PRIVATE
                           "${CMAKE_SOURCE_DIR}/src/runtime/ptmalloc3")
target_link_libraries(fuzzalloc PRIVATE
                      "${CMAKE_BINARY_DIR}/src/runtime/ptmalloc3/libptmalloc3.so")
