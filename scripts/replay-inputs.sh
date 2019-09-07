#!/bin/bash
#
# Replay a set of recorded fuzzing inputs. This script should be run from a
# directory containing a set of Google Fuzzer Test Suite "RUNDIR"s.

export FUZZING_ENGINE="afl"

replay_inputs() {
  RUNDIR=${1}
  TARGET=$(echo -n ${RUNDIR} | cut -c14-)
  echo "Replaying ${TARGET}"

  rm -f ${TARGET}-replay.log

  for I in `seq -f "%08g" 0 499999`; do
    INPUT="${RUNDIR}/collect-seeds/inputs/id_${I}"
    perf stat -r3 "${RUNDIR}/${TARGET}-${FUZZING_ENGINE}" "${INPUT}"
  done > /dev/null 2>> ${TARGET}-replay.log
}

export -f replay_inputs
RUNDIRS="./RUNDIR-${FUZZING_ENGINE}-*"
parallel replay_inputs ::: ${RUNDIRS}
