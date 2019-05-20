add_library(fuzzalloc SHARED malloc.c
                             deref.c)

if(FUZZALLOC_USE_LOCKS)
  add_definitions(-DFUZZALLOC_USE_LOCKS)
endif()

# Need to remove -Werror for afl-llvm-rt.o.c
string(REPLACE " -Werror" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

target_sources(fuzzalloc PUBLIC "${AFL_PATH}/llvm_mode/afl-llvm-rt.o.c")
add_definitions(-DAFL_INSTRUMENT)
