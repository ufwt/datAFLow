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

for SRC_DIR in `ls ${RT_SRC}`; do
    # If the directory matches one in the compiler-rt source, update the files
    # contained within
    if [ -d ${COMPILER_RT_LIB}/${SRC_DIR} ]; then
        for FILE in `ls ${RT_SRC}/${SRC_DIR}/`; do
            ln -sf ${RT_SRC}/${SRC_DIR}/${FILE} ${COMPILER_RT_LIB}/${SRC_DIR}/${FILE}
        done
    fi
done

# Link the fuzzalloc memory allocator
ln -sf ${INC}/fuzzalloc.h ${COMPILER_RT_LIB}/sanitizer_common/fuzzalloc.h
