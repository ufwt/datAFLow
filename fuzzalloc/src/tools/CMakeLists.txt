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
add_executable(dataflow-clang-fast++ dataflow-clang-fast.c)
