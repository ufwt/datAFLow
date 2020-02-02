#!/bin/bash
#
# Run SVF pointer analysis to statically determine all def-use chains

export DIR_NAME=$(readlink -f $(dirname $0))

# Need a dummy fuzzing engine here so that common.sh is sourced correctly
export FUZZING_ENGINE="clang"
. ${DIR_NAME}/common.sh

export ABS_SCRIPT_DIR=$(readlink -f ${SCRIPT_DIR})
export PARENT_DIR="$(readlink -f ${ABS_SCRIPT_DIR}/../ALL_BENCHMARKS-datAFLow)"

analyze_def_use() {
  BENCHMARK=${1}
  TARGET_DIR_NAME=$(basename ${BENCHMARK})
  TARGET=${TARGET_DIR_NAME#RUNDIR-datAFLow-}
  TARGET_PATH="${BENCHMARK}/${TARGET}-datAFLow"

  export FUZZING_ENGINE="datAFLow"
  export FUZZALLOC_TAG_LOG="${PARENT_DIR}/${TARGET}-tag-sites.csv"
  unset CC
  unset CXX
  . ${DIR_NAME}/common.sh

  get-bc ${TARGET_PATH}
  if [ $? -ne 0 ]; then
    echo "Failed to extract BC for ${TARGET}"
    return
  fi

  ${FUZZALLOC_DEBUG_BUILD_DIR}/src/tools/dataflow-wpa "${TARGET_PATH}.bc" > "${BENCHMARK}/wpa.out" 2>&1
}

export -f analyze_def_use
BENCHMARKS="${PARENT_DIR}/RUNDIR-datAFLow-*/"
parallel analyze_def_use ::: ${BENCHMARKS}
