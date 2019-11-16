#!/bin/bash

export FUZZING_ENGINE="afl"
. $(dirname $0)/common.sh

export ABS_SCRIPT_DIR=$(readlink -f ${SCRIPT_DIR})
PARENT_DIR=${1:-"$(readlink -f ${ABS_SCRIPT_DIR}/../ALL_BENCHMARKS-${FUZZING_ENGINE})"}
PARENT_DIR=$(realpath ${PARENT_DIR})

if [ ! -z ${ASAN_ENABLE} ]; then
  PARENT_DIR="${PARENT_DIR}-asan"
fi

export PARENT_DIR

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
