#!/bin/bash -ux

mkdir -p llvm llvm/build llvm/install
cd llvm

$SRC/fuzzalloc/scripts/get_llvm_src.sh

pushd build > /dev/null
cmake ../llvm -DCMAKE_BUILD_TYPE=Release                                            \
    -DCMAKE_INSTALL_PREFIX=$(realpath ../install)                                   \
    -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" -DLLVM_BUILD_EXAMPLES=Off            \
    -DLLVM_INCLUDE_EXAMPLES=Off -DLLVM_BUILD_TESTS=Off -DLLVM_INCLUDE_TESTS=Off     \
    -DLLVM_TARGETS_TO_BUILD=X86 -DFUZZALLOC_ASAN=On                                 \
    -DLIBFUZZALLOC_PATH=$SRC/fuzzalloc-release/src/runtime/malloc/libfuzzalloc.so   \
    -G Ninja
cmake --build .
cmake --build . --target install
popd > /dev/null

rm -rf build
