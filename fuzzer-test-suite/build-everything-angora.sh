#!/bin/bash -x

export DIR_NAME=$(readlink -f $(dirname $0))

# Need a dummy fuzzing engine here so that common.sh is sourced correctly
export FUZZING_ENGINE="clang"
. ${DIR_NAME}/common.sh

export ABS_SCRIPT_DIR=$(readlink -f ${SCRIPT_DIR})

PARENT_DIR=${1:-"$(readlink -f ${ABS_SCRIPT_DIR}/../ALL_BENCHMARKS-angora)"}
PARENT_DIR=$(realpath ${PARENT_DIR})
export PARENT_DIR

mkdir -p ${PARENT_DIR}
echo "Created top directory ${PARENT_DIR}"

build_target() {
  TARGET="$(basename ${1})"

  [[ ! -d ${1} ]] && return
  [[ ! -e ${1}/build.sh ]] && return

  cd ${PARENT_DIR}

  # Build light-instrumentation target
  export FUZZING_ENGINE="angora_fast"
  unset CC
  unset CXX
  . ${DIR_NAME}/common.sh
  echo "Running build ${TARGET} (${FUZZING_ENGINE})"
  ${ABS_SCRIPT_DIR}/build.sh "${TARGET}" > ${TARGET}-${FUZZING_ENGINE}-build.log 2>&1

  # Build taint-tracking target
  export FUZZING_ENGINE="angora_track"
  unset CC
  unset CXX
  . ${DIR_NAME}/common.sh
  echo "Running build ${TARGET} (${FUZZING_ENGINE})"
  ${ABS_SCRIPT_DIR}/build.sh "${TARGET}" > ${TARGET}-${FUZZING_ENGINE}-build.log 2>&1

  # Create the empty seed
  mkdir -p ${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-fast-${TARGET}/empty-seed
  echo "" > ${PARENT_DIR}/RUNDIR-${FUZZING_ENGINE}-fast-${TARGET}/empty-seed/seed
}

export -f build_target
BENCHMARKS="${ABS_SCRIPT_DIR}/*/"
for BENCHMARK in ${BENCHMARKS}; do
  build_target ${BENCHMARK}
done
