#!/bin/bash

set -ex

ROOT_DIR=${PWD}
SRC_DIR=$(dirname "$(readlink -f "${0}")")
AFL_PATH=${SRC_DIR}/../afl-2.52b
CONFIG_DIR=${SRC_DIR}/config

# Activate SPEC CPU2006 environment
source ${ROOT_DIR}/shrc

# Get AFL
export AFL_PATH
make -j -C ${AFL_PATH} clean all
make -j -C ${AFL_PATH}/llvm_mode clean all
ln -sf ${AFL_PATH} afl

# Build the fuzzalloc libraries and tools
mkdir -p fuzzalloc-release && \
  (cd fuzzalloc-release && cmake -DFUZZALLOC_USE_LOCKS=True -DAFL_INSTRUMENT=On -DCMAKE_BUILD_TYPE=Release ${SRC_DIR}/../fuzzalloc &&
  make -j)
mkdir -p fuzzalloc-debug && \
  (cd fuzzalloc-debug && cmake -DFUZZALLOC_USE_LOCKS=True -DAFL_INSTRUMENT=On -DCMAKE_BUILD_TYPE=Debug ${SRC_DIR}/../fuzzalloc &&
  make -j)

# Add helper scripts
ln -sf ${SRC_DIR}/../scripts fuzzalloc-scripts

# Add config files and cleanup previous results
for CONFIG in $(ls ${CONFIG_DIR}); do
  cp -f "${CONFIG_DIR}/${CONFIG}" "config/${CONFIG}"
  runspec --config ${CONFIG} --action=scrub all
done

# Cleanup previous address space shrinkages
rm -rf prefix-*

echo "Setup complete! Now run runspec --config=fuzzalloc --tune=base --noreportable all_except_fortran"
