#!/bin/bash

set -x

if [ -z ${DEBUG} ]; then
    BUILD_TYPE="Release"
else
    BUILD_TYPE="Debug"
fi

ROOT_DIR=${PWD}
SRC_DIR=$(dirname "$(readlink -f "${0}")")

FUZZER_TEST_SUITE="fuzzer-test-suite"
FUZZER_TEST_SUITE_URL="https://github.com/google/${FUZZER_TEST_SUITE}.git"
FUZZER_TEST_SUITE_REV="a47305004cfa75181926a3804207d55b8778814f"

AFL_TAR="afl-latest.tgz"
AFL_URL="http://lcamtuf.coredump.cx/afl/releases/${AFL_TAR}"

COMPILER_RT="compiler-rt"
COMPILER_RT_TAR="compiler-rt-7.0.1.src.tar.xz"
COMPILER_RT_URL="http://releases.llvm.org/7.0.1/${COMPILER_RT_TAR}"

# Get the test suite
if [ ! -d "${FUZZER_TEST_SUITE}" ]; then
    echo "${FUZZER_TEST_SUITE} not found. Downloading the Google fuzzer test suite"
    git clone ${FUZZER_TEST_SUITE_URL} ${FUZZER_TEST_SUITE} && \
        (cd ${FUZZER_TEST_SUITE} && git checkout ${FUZZER_TEST_SUITE_REV})
fi

# Get AFL
if [ -z "${AFL_PATH}" ]; then
    echo "AFL_PATH not set. Downloading and building AFL"
    mkdir -p AFL
    wget ${AFL_URL}
    tar xf ${AFL_TAR} -C AFL --strip-components=1
    rm ${AFL_TAR}

    make -C AFL
    make -C AFL/llvm_mode
    AFL_PATH=${PWD}/AFL
else
    echo "Using AFL at ${AFL_PATH}"
    ln -sf ${AFL_PATH} AFL
fi

# Get compiler-rt
if [ ! -d "${COMPILER_RT}" ]; then
    echo "${COMPILER_RT} not found. Downloading LLVM's compiler-rt"
    mkdir -p ${COMPILER_RT}
    wget ${COMPILER_RT_URL}
    tar xJf ${COMPILER_RT_TAR} -C ${COMPILER_RT} --strip-components=1
    rm ${COMPILER_RT_TAR}
fi
ln -sf compiler-rt/lib/fuzzer Fuzzer

# Build the fuzzalloc libraries and tools
mkdir -p fuzzalloc-build && \
    (cd fuzzalloc-build && cmake -DAFL_INSTRUMENT=On -DCMAKE_BUILD_TYPE=${BUILD_TYPE} ${SRC_DIR}/../fuzzalloc &&
    make -j)

# Replace common.sh with ours (because it supports datAFLow)
ln -sf ${SRC_DIR}/common.sh ${FUZZER_TEST_SUITE}/common.sh