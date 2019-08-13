#!/bin/bash
#
# This script will update the LLVM structure in the current working directory

set -ex

THIS_DIR=$(dirname $(readlink -f ${0}))
SRC=${THIS_DIR}/../src
RT_SRC=${SRC}/compiler-rt-files/
INC=${SRC}/include

LLVM=$(pwd)
RT_DEST=${LLVM}/compiler-rt

ln -sf ${RT_SRC}/lib/asan/asan_allocator.cc ${RT_DEST}/lib/asan/asan_allocator.cc
ln -sf ${RT_SRC}/lib/asan/asan_allocator.h ${RT_DEST}/lib/asan/asan_allocator.h
ln -sf ${RT_SRC}/lib/asan/asan_interceptors.cc ${RT_DEST}/lib/asan/asan_interceptors.cc
ln -sf ${RT_SRC}/lib/asan/asan_malloc_linux.cc ${RT_DEST}/lib/asan/asan_malloc_linux.cc
ln -sf ${RT_SRC}/lib/asan/CMakeLists.txt ${RT_DEST}/lib/asan/CMakeLists.txt
ln -sf ${RT_SRC}/lib/asan/tests/CMakeLists.txt ${RT_DEST}/lib/asan/tests/CMakeLists.txt

ln -sf ${RT_SRC}/lib/sanitizer_common/sanitizer_allocator.h ${RT_DEST}/lib/sanitizer_common/sanitizer_allocator.h
ln -sf ${RT_SRC}/lib/sanitizer_common/sanitizer_fuzzalloc_allocator.h ${RT_DEST}/lib/sanitizer_common/sanitizer_fuzzalloc_allocator.h

ln -sf ${RT_SRC}/test/asan/CMakeLists.txt ${RT_DEST}/lib/asan/CMakeLists.txt
ln -sf ${RT_SRC}/test/asan/lit.site.cfg.in ${RT_DEST}/lib/asan/lit.site.cfg.in
ln -sf ${RT_SRC}/test/asan/lit.cfg ${RT_DEST}/lib/asan/lit.cfg

ln -sf ${INC}/fuzzalloc.h ${RT_DEST}/lib/sanitizer_common/fuzzalloc.h
