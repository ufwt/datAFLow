#!/bin/bash
# Copyright 2017 Google Inc. All Rights Reserved.
# Licensed under the Apache License, Version 2.0 (the "License");

export FUZZING_ENGINE="datAFLow"
export ASAN_ENABLE=1

. $(dirname $0)/common.sh

export ABS_SCRIPT_DIR=$(readlink -f ${SCRIPT_DIR})
export PARENT_DIR="${ABS_SCRIPT_DIR}/../ALL_BENCHMARKS-${FUZZING_ENGINE}"

rm -rf ${PARENT_DIR}
mkdir ${PARENT_DIR}
echo "Created top directory ${PARENT_DIR}"

build_target() {
  TARGET="$(basename ${1})"

  [[ ! -d ${1} ]] && return
  [[ ! -e ${1}/build.sh ]] && return

  export FUZZALLOC_TAG_LOG="${PARENT_DIR}/${TARGET}-tags.csv"

  # Collect tags
  export FUZZING_ENGINE="tags"
  echo "Running build ${TARGET} (${FUZZING_ENGINE})"
  cd ${PARENT_DIR}
  ${ABS_SCRIPT_DIR}/build.sh "${TARGET}" > ${TARGET}-${FUZZING_ENGINE}.log 2>&1

  # Build target
  export FUZZING_ENGINE="datAFLow"
  echo "Running build ${TARGET} (${FUZZING_ENGINE})"
  ${ABS_SCRIPT_DIR}/build.sh "${TARGET}" > ${TARGET}-${FUZZING_ENGINE}.log 2>&1

  # Create the empty seed
  mkdir -p ${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-${TARGET}/empty-seed
  echo "" > ${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-${TARGET}/empty-seed/seed
}

export -f build_target
BENCHMARKS="${ABS_SCRIPT_DIR}/*/"
parallel build_target ::: "${BENCHMARKS}"
