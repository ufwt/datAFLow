#!/bin/bash

set -ex

ROOT_DIR=${PWD}
SRC_DIR=$(dirname "$(readlink -f "${0}")")
CONFIG_DIR=${SRC_DIR}/config

# Activate SPEC CPU2006 environment
source ${ROOT_DIR}/shrc

# Get and build AFL
if [[ ! -d "afl" ]]; then
  git clone --depth=1 https://github.com/google/AFL afl
  make -C afl -j && make -C afl/llvm_mode -j
fi

# Get and build datAFLow
if [[ ! -d "datAFLow" ]]; then
  git clone --depth=1 https://github.com/HexHive/datAFlow datAFLow
fi

mkdir -p fuzzalloc-release && \
  (cd fuzzalloc-release &&
  cmake -DFUZZALLOC_USE_LOCKS=False -DAFL_INSTRUMENT=On \
    -DCMAKE_BUILD_TYPE=Release datAFLow/fuzzalloc &&
  make -j)
mkdir -p fuzzalloc-debug && \
  (cd fuzzalloc-debug &&
  cmake -DFUZZALLOC_USE_LOCKS=False -DAFL_INSTRUMENT=On \
    -DCMAKE_BUILD_TYPE=Debug datAFLow/fuzzalloc &&
  make -j)

# Add helper scripts
ln -sf datAFLow/scripts fuzzalloc-scripts

# Add config files and cleanup previous results
for CONFIG in $(ls ${CONFIG_DIR}); do
  cp -f "${CONFIG_DIR}/${CONFIG}" "config/${CONFIG}"
  runspec --config ${CONFIG} --action=scrub all
done

# Cleanup previous address space shrinkages
rm -rf prefix-*

echo "Setup complete! Now run of \`runspec --config=fuzzalloc-{access, access-heapify-structs, access-idx, access-idx-heapify-structs} --tune=base --noreportable all_except_fortran\`"
