#!/bin/bash

set -ex

ROOT_DIR=${PWD}
SRC_DIR=$(dirname "$(readlink -f "${0}")")
WHITELIST_DIR="${SRC_DIR}/whitelists"

FUZZER_TEST_SUITE="fuzzer-test-suite"
FUZZER_TEST_SUITE_URL="https://github.com/google/${FUZZER_TEST_SUITE}.git"
FUZZER_TEST_SUITE_REV="a4c174b24f36798c8f9121b6246503c17ef5c3e9"

AFL_PATH=${SRC_DIR}/../afl-2.52b

COMPILER_RT="compiler-rt"
COMPILER_RT_TAR="compiler-rt-7.0.1.src.tar.xz"
COMPILER_RT_URL="http://releases.llvm.org/7.0.1/${COMPILER_RT_TAR}"

ANGORA="angora"
ANGORA_URL="https://github.com/AngoraFuzzer/Angora.git"

# Get the test suite
if [ ! -d "${FUZZER_TEST_SUITE}" ]; then
  echo "${FUZZER_TEST_SUITE} not found. Downloading the Google fuzzer test suite"
  git clone ${FUZZER_TEST_SUITE_URL} ${FUZZER_TEST_SUITE} && \
    (cd ${FUZZER_TEST_SUITE} && git checkout ${FUZZER_TEST_SUITE_REV})
fi

# Get AFL
export AFL_PATH
make -j -C ${AFL_PATH} clean all
make -j -C ${AFL_PATH}/llvm_mode clean all
ln -sf ${AFL_PATH} AFL

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

# Get Angora
if [ -z "${ANGORA_BUILD_DIR}" ]; then
  if [ ! -d "${ANGORA}" ]; then
    echo "${ANGOAR} not found. Downloading"
    git clone ${ANGORA_URL} ${ANGORA}
  fi

  export ANGORA_BUILD_DIR=${PWD}/${ANGORA}
fi
ln -sf ${SRC_DIR}/angora/angora_clang.c ${ANGORA_BUILD_DIR}/llvm_mode/compiler/angora_clang.c

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
ln -sf ${SRC_DIR}/build-everything-afl.sh ${FUZZER_TEST_SUITE}/build-everything-afl.sh
ln -sf ${SRC_DIR}/build-everything-datAFLow.sh ${FUZZER_TEST_SUITE}/build-everything-datAFLow.sh
ln -sf ${SRC_DIR}/build-everything-datAFLow.sh ${FUZZER_TEST_SUITE}/build-everything-angora.sh

# Add Angora driver
ln -sf ${SRC_DIR}/angora/angora_driver.c angora_driver.c

# Add whitelists into the appropriate directory
for WHITELIST in $(ls ${WHITELIST_DIR}); do
  ln -sf "${WHITELIST_DIR}/${WHITELIST}" "${FUZZER_TEST_SUITE}/${WHITELIST%.txt}/whitelist.txt"
done
