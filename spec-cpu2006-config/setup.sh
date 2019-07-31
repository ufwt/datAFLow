#!/bin/bash

set -ex

ROOT_DIR=${PWD}
SRC_DIR=$(dirname "$(readlink -f "${0}")")
AFL_PATH=${SRC_DIR}/../afl-2.52b
CONFIG_DIR=${SRC_DIR}/config

# Activate SPEC CPU2006 environment
source ./shrc

# Get AFL
export AFL_PATH
make -C ${AFL_PATH}
make -C ${AFL_PATH}/llvm_mode
ln -sf ${AFL_PATH} afl

# Build the fuzzalloc libraries and tools
mkdir -p fuzzalloc-release && \
  (cd fuzzalloc-release && cmake -DFUZZALLOC_USE_LOCKS=True -DAFL_INSTRUMENT=On -DCMAKE_BUILD_TYPE=Release ${SRC_DIR}/../fuzzalloc &&
  make -j)
mkdir -p fuzzalloc-debug && \
  (cd fuzzalloc-debug && cmake -DFUZZALLOC_USE_LOCKS=True -DAFL_INSTRUMENT=On -DCMAKE_BUILD_TYPE=Debug ${SRC_DIR}/../fuzzalloc &&
  make -j)

# Add config files
for CONFIG in $(ls ${CONFIG_DIR}); do
  cp -f "${CONFIG_DIR}/${CONFIG}" "config/${CONFIG}"
done
