#!/bin/bash
# Copyright 2017 Google Inc. All Rights Reserved.
# Licensed under the Apache License, Version 2.0 (the "License");

export DIR_NAME=$(readlink -f $(dirname $0))

# Need a dummy fuzzing engine here so that common.sh is sourced correctly
export FUZZING_ENGINE="clang"
. ${DIR_NAME}/common.sh

export ABS_SCRIPT_DIR=$(readlink -f ${SCRIPT_DIR})
PARENT_DIR="$(readlink -f ${ABS_SCRIPT_DIR}/../ALL_BENCHMARKS-datAFLow)"

if [ ! -z ${ASAN_ENABLE} ]; then
  PARENT_DIR="${PARENT_DIR}-asan"
fi

export PARENT_DIR

rm -rf ${PARENT_DIR}
mkdir ${PARENT_DIR}
echo "Created top directory ${PARENT_DIR}"

build_target() {
  TARGET="$(basename ${1})"

  [[ ! -d ${1} ]] && return
  [[ ! -e ${1}/build.sh ]] && return

  cd ${PARENT_DIR}

  # Collect tags
  export FUZZING_ENGINE="tags"
  export FUZZALLOC_TAG_LOG="${PARENT_DIR}/${TARGET}-tag-sites.csv"
  unset CC
  unset CXX
  . ${DIR_NAME}/common.sh
  echo "Running build ${TARGET} (${FUZZING_ENGINE})"
  ${ABS_SCRIPT_DIR}/build.sh "${TARGET}" > ${TARGET}-${FUZZING_ENGINE}-build.log 2>&1

  # Build target
  export FUZZING_ENGINE="datAFLow"
  unset CC
  unset CXX
  . ${DIR_NAME}/common.sh
  echo "Running build ${TARGET} (${FUZZING_ENGINE})"
  ${ABS_SCRIPT_DIR}/build.sh "${TARGET}" > ${TARGET}-${FUZZING_ENGINE}-build.log 2>&1

  # Prelink target
  RUNDIR="${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-${TARGET}"
  echo "Prelinking ${TARGET}"
  (export LD_LIBRARY_PATH=${FUZZALLOC_RELEASE_BUILD_DIR}/src/runtime/malloc;    \
    ${DIR_NAME}/../fuzzalloc-scripts/prelink_binary.py                          \
    --set-rpath --in-place --out-dir ${RUNDIR}/prelink --base-addr 0x7fff7000   \
    ${RUNDIR}/${TARGET}-${FUZZING_ENGINE})

  # Create the empty seed
  mkdir -p ${RUNDIR}/empty-seed
  echo "" > ${RUNDIR}/empty-seed/seed
}

export -f build_target
BENCHMARKS="${ABS_SCRIPT_DIR}/*/"
for BENCHMARK in ${BENCHMARKS}; do
  build_target ${BENCHMARK}
done
