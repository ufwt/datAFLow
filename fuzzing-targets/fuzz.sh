#!/bin/bash -ex

if [ "$#" -ne 1 ]; then
    echo "usage: $0 TARGET"
    exit 1
fi

SCRIPT_PATH=$(dirname $(realpath -s $0))
TARGET=$1
TIMEOUT=86400
JOBS=30
SEEDS="${TARGET}/seeds"

export AFL_NO_UI=1
. ${SCRIPT_PATH}/${TARGET}/fuzz-config.sh

# AFL fuzz
for I in $(seq 1 5); do
    sem --timeout ${TIMEOUT} --jobs ${JOBS} --id "fuzz-${TARGET}" -u    \
    /usr/bin/time --verbose --output=${TARGET}/afl-$I.time              \
    AFL/afl-fuzz -m none                                                \
        -i ${SEEDS} -o ${TARGET}/afl-out-$I --                          \
        ${TARGET}/${AFL_TARGET} ${TARGET_OPTS}                          \
        > ${TARGET}/afl-$I.log 2>&1
    sleep 2
done

# MOpt fuzz
for I in $(seq 1 5); do
    sem --timeout ${TIMEOUT} --jobs ${JOBS} --id "fuzz-${TARGET}" -u -q \
    /usr/bin/time --verbose --output=${TARGET}/mopt-$I.time             \
    "MOpt-AFL/MOpt-AFL V1.0/afl-fuzz" -L 0 -m none                      \
        -i ${SEEDS} -o ${TARGET}/mopt-out-$I                            \
        -- ${TARGET}/${AFL_TARGET} ${TARGET_OPTS}                       \
        > ${TARGET}/mopt-$I.log 2>&1
    sleep 2
done

# Need to fix up the interpreter
patchelf --set-interpreter                                  \
    "${TARGET}/${DATAFLOW_ACCESS_LD}/ld-linux-x86-64.so.2"  \
    ${TARGET}/${DATAFLOW_ACCESS_TARGET}

# datAFLow access fuzz
for I in $(seq 1 5); do
    LD_LIBRARY_PATH=${TARGET}/${DATAFLOW_ACCESS_LD}:${LD_LIBRARY_PATH}  \
    sem --timeout ${TIMEOUT} --jobs ${JOBS} --id "fuzz-${TARGET}" -u    \
    /usr/bin/time --verbose --output=${TARGET}/datAFLow-access-$I.time  \
    AFL/afl-fuzz -m none                                                \
        -i ${SEEDS} -o ${TARGET}/datAFLow-access-out-$I --              \
        ${TARGET}/${DATAFLOW_ACCESS_TARGET} ${TARGET_OPTS}              \
        > ${TARGET}/datAFLow-access-$I.log 2>&1
    sleep 2
done

# datAFLow access MOpt fuzz
for I in $(seq 1 5); do
    LD_LIBRARY_PATH=${TARGET}/${DATAFLOW_ACCESS_LD}:${LD_LIBRARY_PATH}      \
    sem --timeout ${TIMEOUT} --jobs ${JOBS} --id "fuzz-${TARGET}" -u -q     \
    /usr/bin/time --verbose --output=${TARGET}/datAFLow-access-mopt-$I.time \
    "MOpt-AFL/MOpt-AFL V1.0/afl-fuzz" -L 0 -m none                          \
        -i ${SEEDS} -o ${TARGET}/datAFLow-access-mopt-out-$I --             \
        ${TARGET}/${DATAFLOW_ACCESS_TARGET} ${TARGET_OPTS}                  \
        > ${TARGET}/datAFLow-access-mopt-$I.log 2>&1
    sleep 2
done

# Need to fix up the interpreter
patchelf --set-interpreter                                      \
    "${TARGET}/${DATAFLOW_ACCESS_IDX_LD}/ld-linux-x86-64.so.2"  \
    ${TARGET}/${DATAFLOW_ACCESS_IDX_TARGET}

# datAFLow access + index fuzz
for I in $(seq 1 5); do
    LD_LIBRARY_PATH=${TARGET}/${DATAFLOW_ACCESS_IDX_LD}:${LD_LIBRARY_PATH}  \
    sem --timeout ${TIMEOUT} --jobs ${JOBS} --id "fuzz-${TARGET}" -u        \
    timeout --preserve-status 24h                                           \
    AFL/afl-fuzz -m none                                                    \
        -i ${SEEDS} -o ${TARGET}/datAFLow-access-idx-out-$I --              \
        ${TARGET}/${DATAFLOW_ACCESS_TARGET} ${TARGET_OPTS}                  \
        > ${TARGET}/datAFLow-access-idx-$I.log 2>&1
    sleep 2
done

# datAFLow access + index MOpt fuzz
for I in $(seq 1 5); do
    LD_LIBRARY_PATH=${TARGET}/${DATAFLOW_ACCESS_IDX_LD}:${LD_LIBRARY_PATH}      \
    sem --timeout ${TIMEOUT} --jobs ${JOBS} --id "fuzz-${TARGET}" -u -q         \
    /usr/bin/time --verbose --output=${TARGET}/datAFLow-access-idx-mopt-$I.time \
    "MOpt-AFL/MOpt-AFL V1.0/afl-fuzz" -L 0 -m none                              \
        -i ${SEEDS} -o ${TARGET}/datAFLow-access-idx-mopt-out-$I --             \
        ${TARGET}/${DATAFLOW_ACCESS_IDX_TARGET} ${TARGET_OPTS}                  \
        > ${TARGET}/datAFLow-access-idx-mopt-$I.log 2>&1
    sleep 2
done

# Wait for campaigns to finish
sem --wait --id "fuzz-${TARGET}"
