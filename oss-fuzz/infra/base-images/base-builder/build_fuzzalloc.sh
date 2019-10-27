#!/bin/bash -eux

export PATH="$SRC/llvm/install/bin:$PATH"
export LD_LIBRARY_PATH="$SRC/llvm/install/lib:${LD_LIBRARY_PATH-}"
export AFL_PATH="$SRC/afl"

mkdir -p fuzzalloc-release
(cd fuzzalloc-release &&                                                    \
    cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++             \
    -DFUZZALLOC_USE_LOCKS=False -DAFL_INSTRUMENT=On                         \
    -DCMAKE_BUILD_TYPE=Release $SRC/fuzzalloc &&                            \
    cmake --build .)

mkdir -p fuzzalloc-debug
(cd fuzzalloc-debug &&                                                      \
    cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++             \
    -DFUZZALLOC_USE_LOCKS=False -DAFL_INSTRUMENT=On                         \
    -DCMAKE_BUILD_TYPE=Debug $SRC/fuzzalloc &&                              \
    cmake --build .)
