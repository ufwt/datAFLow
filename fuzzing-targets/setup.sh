#!/bin/bash -eux

SRC_DIR=$(dirname "$(readlink -f "${0}")")

# Get and build AFL
git clone --depth=1 https://github.com/google/AFL
make -C AFL -j && make -C AFL/llvm_mode -j

# Get and build MOpt
git clone --depth=1 https://github.com/puppet-meteor/MOpt_AFL
make -C MOpt-AFL/MOpt-AFL\ V1.0/ -j && make -C MOpt-AFL/MOpt-AFL\ V1.0/llvm_mode -j

# Get the set of fuzzing scripts
git clone --depth=1 https://github.com/HexHive/fuzzing-data-analysis

# Build the base Docker images
docker build -t dataflow/base ${SRC_DIR} --build-arg SSH_PRIVATE_KEY="$(cat ~/.ssh/id_rsa)"

# Build the target Docker images
find * -mindepth 1 -name Dockerfile -print0 |   \
    sort -z |                                   \
    xargs -r0 -I '{}' bash -c 'docker build -t dataflow/$(dirname $0) $(dirname $0)' {}

ln -sf ${SRC_DIR}/fuzz.sh .
ln -sf ${SRC_DIR}/cross_coverage.sh .

echo "Now fuzz your chosen target using the `fuzz.sh` script"
