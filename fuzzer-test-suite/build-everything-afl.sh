#!/bin/bash

export FUZZING_ENGINE="afl"
export ASAN_ENABLE=1

. $(dirname $0)/common.sh

export ABS_SCRIPT_DIR=$(readlink -f ${SCRIPT_DIR})
export PARENT_DIR="$(readlink -f ${ABS_SCRIPT_DIR}/../ALL_BENCHMARKS-${FUZZING_ENGINE})"

mkdir -p ${PARENT_DIR}
echo "Created top directory ${PARENT_DIR}"

build_target() {
  TARGET="$(basename ${1})"

  [[ ! -d ${1} ]] && return
  [[ ! -e ${1}/build.sh ]] && return

  # Build target
  echo "Building ${TARGET}"
  cd ${PARENT_DIR}
  ${ABS_SCRIPT_DIR}/build.sh "${TARGET}" > ${TARGET}-${FUZZING_ENGINE}-build.log 2>&1

  # Create the empty seed
  mkdir -p ${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-${TARGET}/empty-seed
  echo "" > ${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-${TARGET}/empty-seed/seed
}

export -f build_target
BENCHMARKS="${ABS_SCRIPT_DIR}/*/"
parallel build_target ::: ${BENCHMARKS}
