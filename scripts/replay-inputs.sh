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
  REPLAY_LOG="${TARGET}-${FUZZING_ENGINE}-replay.log"

  echo "Replaying ${TARGET}"

  rm -f ${REPLAY_LOG}

  for I in 0 25001 50001 75001 100001 125001 150001 175001 200001 225001 250001 275001 300001 325001 350001 375001 400001 425001 450001 475001; do
    INPUTS=$(find "${RUNDIR}/collect-seeds/inputs" -type f | sort | tail -n +$I | head -25000)
    perf stat -r3 --output ${REPLAY_LOG} --append  \
      "${RUNDIR}/${TARGET}-${FUZZING_ENGINE}" ${INPUTS} > /dev/null 2>&1
  done
}

export -f replay_inputs
RUNDIRS="./RUNDIR-${FUZZING_ENGINE}-*"
parallel replay_inputs ::: ${RUNDIRS}
