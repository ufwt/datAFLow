# Need to remove -Werror when using AFL source code
string(REPLACE " -Werror" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Get some directory paths to pass to dataflow-clang-fast
get_filename_component(FUZZALLOC_LLVM_DIR
                       "${CMAKE_CURRENT_BINARY_DIR}/../llvm-files/lib/"
                       ABSOLUTE)
get_filename_component(FUZZALLOC_MALLOC_DIR
                       "${CMAKE_CURRENT_BINARY_DIR}/../runtime/malloc"
                       ABSOLUTE)

add_definitions(-DFUZZALLOC_LLVM_DIR="${FUZZALLOC_LLVM_DIR}"
                -DFUZZALLOC_MALLOC_DIR="${FUZZALLOC_MALLOC_DIR}")

add_executable(dataflow-clang-fast dataflow-clang-fast.c common.c)
add_executable(dataflow-clang-fast++ dataflow-clang-fast.c common.c)
add_executable(dataflow-collect-tag-sites dataflow-collect-tag-sites.c common.c)
add_executable(dataflow-collect-tag-sites++ dataflow-collect-tag-sites.c common.c)
add_executable(dataflow-wpa dataflow-wpa.c)
