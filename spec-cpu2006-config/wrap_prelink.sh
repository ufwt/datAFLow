#!/bin/bash

PRELINK_BINARY="${SPEC}/fuzzalloc-release/scripts/prelink_binary.py"
[ ! -f ${PRELINK_BINARY} ] && echo "Could not find prelink_binary.py - check that you've sourced shrc" && exit 1

[ $# -lt 1 ] && echo "Usage: $0 binary [args]" && exit 1

PROG=$1
shift
if [ ! -f ${PROG} ]; then
  PROG=$(which "${PROG}")
  [ ! -f ${PROG} ] && echo "Could not find ${PROG} here nor in PATH" && exit 1
fi

PRELINK_DIR="prelink-$(echo ${PROG} | tr / _)"
"${SPEC}/fuzzalloc-release/scripts/prelink_binary.py" --set-rpath "${PROG}"

export LD_LIBRARY_PATH="$(pwd)/${PRELINK_DIR}:$LD_LIBRARY_PATH"

NEW_PROG="${PRELINK_DIR}/$(basename "${PROG}")"
exec "${NEW_PROG}" "$@"
