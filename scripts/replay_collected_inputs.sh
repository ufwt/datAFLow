#!/bin/bash
#
# Replay a set of recorded fuzzing inputs. This script should be run from a
# directory containing a set of Google Fuzzer Test Suite "RUNDIR"s.

if [ -z ${FUZZING_ENGINE} ]; then
  echo "Please set the FUZZING_ENGINE environment variable"
  exit 1
fi

replay_inputs() {
  RUNDIR=${1}
  TARGET=$(echo -n ${RUNDIR} | sed "s/RUNDIR-${FUZZING_ENGINE}-//g")
  INPUT_DIR="${RUNDIR}/collect-seeds/inputs"
  REPLAY_LOG="${TARGET}-${FUZZING_ENGINE}-replay.log"

  [[ ! -d ${INPUT_DIR} ]] && return

  rm -f ${REPLAY_LOG}

  for I in 0 25001 50001 75001 100001 125001 150001 175001 200001 225001 250001 275001 300001 325001 350001 375001 400001 425001 450001 475001; do
    INPUTS=$(find ${INPUT_DIR} -type f | sort | tail -n +$I | head -25000)
    perf stat -r3 --output ${REPLAY_LOG} --append  -- \
      "${RUNDIR}/${TARGET}-${FUZZING_ENGINE}" ${INPUTS} 1> "${TARGET}-${FUZZING_ENGINE}-out.log" 2> "${TARGET}-${FUZZING_ENGINE}-err.log"
  done

  TOTAL_TIME=$(awk -F ' ' '$0 ~ /task-clock/ {TOTAL_TIME += $1} END {printf "%f", TOTAL_TIME}' ${REPLAY_LOG})
  echo "${TARGET}, ${TOTAL_TIME}"
}

export -f replay_inputs
RUNDIRS="RUNDIR-${FUZZING_ENGINE}-*"
parallel replay_inputs ::: ${RUNDIRS}
