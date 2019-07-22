#!/bin/bash

set -ex

ROOT_DIR=${PWD}
SRC_DIR=$(dirname "$(readlink -f "${0}")")
WHITELIST_DIR="${SRC_DIR}/whitelists"

FUZZER_TEST_SUITE="fuzzer-test-suite"
FUZZER_TEST_SUITE_URL="https://github.com/google/${FUZZER_TEST_SUITE}.git"
FUZZER_TEST_SUITE_REV="729dea6c49521cf6965d71a0ffb2c4dc52bd8222"

AFL="AFL"
AFL_TAR="afl-latest.tgz"
AFL_URL="http://lcamtuf.coredump.cx/afl/releases/${AFL_TAR}"

COMPILER_RT="compiler-rt"
COMPILER_RT_TAR="compiler-rt-7.0.1.src.tar.xz"
COMPILER_RT_URL="http://releases.llvm.org/7.0.0/${COMPILER_RT_TAR}"

# Get the test suite
if [ ! -d "${FUZZER_TEST_SUITE}" ]; then
  echo "${FUZZER_TEST_SUITE} not found. Downloading the Google fuzzer test suite"
  git clone ${FUZZER_TEST_SUITE_URL} ${FUZZER_TEST_SUITE} && \
    (cd ${FUZZER_TEST_SUITE} && git checkout ${FUZZER_TEST_SUITE_REV})
fi

# Get AFL
if [ -z "${AFL_PATH}" ]; then
  if [ ! -d "${AFL}" ]; then
    echo "AFL_PATH not set. Downloading and building AFL"
    mkdir -p ${AFL}
    wget ${AFL_URL}
    tar xf ${AFL_TAR} -C AFL --strip-components=1
    rm ${AFL_TAR}

    make -C ${AFL} clean all
    make -C ${AFL}/llvm_mode clean all
  fi

  export AFL_PATH=${PWD}/${AFL}
else
  echo "Using existing AFL at ${AFL_PATH}"
  if [ ! -f "${AFL_PATH}/afl-fuzz.c" ]; then
    echo "Invalid AFL path"
    exit 1
  fi

  make -C ${AFL_PATH}
  make -C ${AFL_PATH}/llvm_mode

  ln -sf ${AFL_PATH} AFL
fi

# Get compiler-rt
if [ -z "${COMPILER_RT_PATH}" ]; then
  if [ ! -d "${COMPILER_RT}" ]; then
    echo "COMPILER_RT not set. Downloading LLVM's compiler-rt"
    mkdir -p ${COMPILER_RT}
    wget ${COMPILER_RT_URL}
    tar xJf ${COMPILER_RT_TAR} -C ${COMPILER_RT} --strip-components=1
    rm ${COMPILER_RT_TAR}
  fi

  export COMPILER_RT_PATH=${PWD}/${COMPILER_RT}
else
  echo "Using existing LLVM compiler-rt at ${COMPILER_RT_PATH}"
  if [ ! -f "${COMPILER_RT_PATH}/lib/fuzzer/afl/afl_driver.cpp" ]; then
    echo "Invalid LLVM compiler-rt path"
    exit 1
  fi
fi
ln -sf ${COMPILER_RT_PATH}/lib/fuzzer Fuzzer

# Build the fuzzalloc libraries and tools
mkdir -p fuzzalloc-release && \
  (cd fuzzalloc-release && cmake -DFUZZALLOC_USE_LOCKS=True -DAFL_INSTRUMENT=On -DCMAKE_BUILD_TYPE=Release ${SRC_DIR}/../fuzzalloc &&
  make -j)
mkdir -p fuzzalloc-debug && \
  (cd fuzzalloc-debug && cmake -DFUZZALLOC_USE_LOCKS=True -DAFL_INSTRUMENT=On -DCMAKE_BUILD_TYPE=Debug ${SRC_DIR}/../fuzzalloc &&
  make -j)

# Replace common.sh with ours (because it supports datAFLow)
ln -sf ${SRC_DIR}/common.sh ${FUZZER_TEST_SUITE}/common.sh

# Add scripts to just build stuff
ln -sf ${SRC_DIR}/build.sh ${FUZZER_TEST_SUITE}/build.sh
ln -sf ${SRC_DIR}/build-everything.sh ${FUZZER_TEST_SUITE}/build-everything.sh

# Add whitelists into the appropriate directory
for WHITELIST in $(ls ${WHITELIST_DIR}); do
  ln -sf "${WHITELIST_DIR}/${WHITELIST}" "${FUZZER_TEST_SUITE}/${WHITELIST%.txt}/whitelist.txt"
done
