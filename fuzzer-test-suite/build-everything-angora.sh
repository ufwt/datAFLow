#!/bin/bash
# Copyright 2017 Google Inc. All Rights Reserved.
# Licensed under the Apache License, Version 2.0 (the "License");

export FUZZING_ENGINE="angora"

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

  # Build taint-tracking target
  export USE_TRACK=1
  echo "Running build ${TARGET} (track)"
  ${ABS_SCRIPT_DIR}/build.sh "${TARGET}" > ${TARGET}-track.log 2>&1
  mv ${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-${TARGET} ${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-track-${TARGET}

  # Build light-instrumentation target
  export USE_FAST=1
  echo "Running build ${TARGET} (fast)"
  ${ABS_SCRIPT_DIR}/build.sh "${TARGET}" > ${TARGET}-fast.log 2>&1
  mv ${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-${TARGET} ${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-fast-${TARGET}

  # Create the empty seed
  mkdir -p ${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-fast-${TARGET}/empty-seed
  echo "" > ${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-fast-${TARGET}/empty-seed/seed
}

export -f build_target
BENCHMARKS="${ABS_SCRIPT_DIR}/*/"
parallel build_target ::: "${BENCHMARKS}"
