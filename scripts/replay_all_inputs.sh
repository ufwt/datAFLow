#!/bin/bash
#
# Replay a set of recorded fuzzing inputs. This script should be run from a
# directory containing a set of Google Fuzzer Test Suite "RUNDIR"s.

SCRIPT_DIR=$(dirname $(realpath -s $0)

. "${SCRIPT_DIR}/replay_inputs.sh"

export -f replay_inputs
RUNDIRS="RUNDIR-${FUZZING_ENGINE}-*"
#parallel replay_inputs ::: ${RUNDIRS}

for RUNDIR in ${RUNDIRS}; do
  replay_inputs ${RUNDIR}
done
