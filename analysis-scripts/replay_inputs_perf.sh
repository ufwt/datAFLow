#!/bin/bash
#
# Replay inputs for performance measurements.

if [ -z ${FUZZING_ENGINE} ]; then
  echo "Please set the FUZZING_ENGINE environment variable"
  exit 1
fi

# Same ASan flags used during fuzzing
export ASAN_OPTIONS="abort_on_error=1:detect_leaks=0:symbolize=0:allocator_may_return_null=1"

replay_inputs() {
  RUNDIR=${1}
  TARGET=$(echo -n ${RUNDIR} | sed "s/RUNDIR-${FUZZING_ENGINE}-//g")
  INPUT_DIR="${RUNDIR}/collect-seeds/inputs"
  REPLAY_LOG="${TARGET}-${FUZZING_ENGINE}-replay.log"
  INC=${INC:-1000}

  [[ ! -d ${INPUT_DIR} ]] && return

  rm -f ${REPLAY_LOG}

  for I in $(seq 0 ${INC} 500000); do
    INPUTS=$(find ${INPUT_DIR} -type f | sort | tail -n +${I}| head -${INC})
    perf stat -r3 --output ${REPLAY_LOG} --append  -- \
      "${RUNDIR}/${TARGET}-${FUZZING_ENGINE}" ${INPUTS} 1> "${TARGET}-${FUZZING_ENGINE}-out.log" 2> "${TARGET}-${FUZZING_ENGINE}-err.log"
  done

  TOTAL_TIME=$(awk -F ' ' '$0 ~ /task-clock/ {TOTAL_TIME += $1} END {printf "%f", TOTAL_TIME}' ${REPLAY_LOG})
  echo "${TARGET}, ${TOTAL_TIME}"
}

if [ "${BASH_SOURCE[0]}" -ef "$0" ]; then
  echo "Replaying inputs for $1 (perf)..."
  replay_inputs $1
fi
