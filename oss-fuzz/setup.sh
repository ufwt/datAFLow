#!/bin/bash -eux

SRC_DIR=$(dirname "$(readlink -f "${0}")")

OSS_FUZZ="oss-fuzz"
OSS_FUZZ_BUILDER_DIR="${OSS_FUZZ}/infra/base-images/base-builder"
OSS_FUZZ_URL="https://github.com/google/${OSS_FUZZ}.git"

# Download OSS-Fuzz
if [ ! -d "${OSS_FUZZ}" ]; then
  echo "${OSS_FUZZ} not found. Downloading"
  git clone --single-branch --depth 1 -- ${OSS_FUZZ_URL} ${OSS_FUZZ}
fi

# Update OSS-Fuzz files
cp ${SRC_DIR}/infra/helper.py ${OSS_FUZZ}/infra

# Copy everything needed
rsync -av ${SRC_DIR}/infra/base-images/base-builder/ ${OSS_FUZZ_BUILDER_DIR}
rsync -av ${SRC_DIR}/../fuzzalloc ${OSS_FUZZ_BUILDER_DIR}/
rsync -av ${SRC_DIR}/../SVF ${OSS_FUZZ_BUILDER_DIR}/
rsync -av ${SRC_DIR}/../scripts ${OSS_FUZZ_BUILDER_DIR}/
