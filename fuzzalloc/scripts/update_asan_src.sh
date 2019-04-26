#!/bin/bash

set -ex

THIS_DIR=$(dirname $(readlink -f ${0}))
SRC=${THIS_DIR}/../src
RT_SRC=${SRC}/runtime
INC=${SRC}/include

LLVM=$(pwd)/llvm
CLANG=$(pwd)/llvm/tools/clang
COMPILER_RT=$(pwd)/llvm/projects/compiler-rt
COMPILER_RT_LIB=${COMPILER_RT}/lib
COMPILER_RT_TEST=${COMPILER_RT}/test

ln -sf ${RT_SRC}/asan/asan_allocator.cc ${COMPILER_RT_LIB}/asan/asan_allocator.cc
ln -sf ${RT_SRC}/asan/asan_allocator.h ${COMPILER_RT_LIB}/asan/asan_allocator.h
ln -sf ${RT_SRC}/asan/asan_interceptors.cc ${COMPILER_RT_LIB}/asan/asan_interceptors.cc
ln -sf ${RT_SRC}/asan/asan_malloc_linux.cc ${COMPILER_RT_LIB}/asan/asan_malloc_linux.cc
ln -sf ${RT_SRC}/asan/CMakeLists.txt ${COMPILER_RT_LIB}/asan/CMakeLists.txt
ln -sf ${RT_SRC}/asan/tests/CMakeLists.txt ${COMPILER_RT_LIB}/asan/tests/CMakeLists.txt

ln -sf ${RT_SRC}/sanitizer_common/sanitizer_allocator.h ${COMPILER_RT_LIB}/sanitizer_common/sanitizer_allocator.h
ln -sf ${RT_SRC}/sanitizer_common/sanitizer_fuzzalloc_allocator.h ${COMPILER_RT_LIB}/sanitizer_common/sanitizer_fuzzalloc_allocator.h

ln -sf ${RT_SRC}/test/asan/CMakeLists.txt ${COMPILER_RT_TEST}/asan/CMakeLists.txt
ln -sf ${RT_SRC}/test/asan/lit.site.cfg.in ${COMPILER_RT_TEST}/asan/lit.site.cfg.in
ln -sf ${RT_SRC}/test/asan/lit.cfg ${COMPILER_RT_TEST}/asan/lit.cfg

ln -sf ${INC}/fuzzalloc.h ${COMPILER_RT_LIB}/sanitizer_common/fuzzalloc.h
