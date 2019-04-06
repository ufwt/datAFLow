add_library(fuzzalloc SHARED malloc.c
                             deref.c)

if(POOL_SIZE)
  message(STATUS "Allocation pool size set to ${POOL_SIZE} bytes")
  add_definitions(-DPOOL_SIZE=${POOL_SIZE})
endif(POOL_SIZE)

if(USE_LOCKS)
  add_definitions(-DUSE_LOCKS)
endif()

# Need to remove -Werror for afl-llvm-rt.o.c
string(REPLACE " -Werror" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

target_sources(fuzzalloc PUBLIC "${AFL_PATH}/llvm_mode/afl-llvm-rt.o.c")
