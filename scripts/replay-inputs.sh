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

  for I in 0 25001 50001 75001 100001 125001 150001 175001 200001 225001 250001 275001 300001 325001 350001 375001 400001 425001 450001 475001; do
    perf stat -r3 "${RUNDIR}/${TARGET}-${FUZZING_ENGINE}" $(find "${RUNDIR}/collect-seeds/inputs" -type f | sort | tail -n +$I | head -25000) > /dev/null 2>> ${TARGET}-replay.log
  done
}

export -f replay_inputs
RUNDIRS="./RUNDIR-${FUZZING_ENGINE}-*"
parallel replay_inputs ::: ${RUNDIRS}
