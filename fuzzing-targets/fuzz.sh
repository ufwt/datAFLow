#!/bin/bash -ex

. ${WORKDIR}/fuzz-config.sh

# AFL fuzz
for I in $(seq 1 5); do
    nohup /usr/bin/time --verbose --output=${WORKDIR}/afl-$I.time   \
        timeout --preserve-status 24h                               \
        ${WORKDIR}/AFL/afl-fuzz -m none                             \
            -i ${WORKDIR}/seeds -o ${WORKDIR}/afl-out-$I --         \
            ${AFL_TARGET} ${TARGET_OPTS} > ${WORKDIR}/afl-$I.log 2>&1 &
    sleep 2
done

# MOpt fuzz
for I in $(seq 1 5); do
    nohup /usr/bin/time --verbose --output=${WORKDIR}/mopt-$I.time  \
        timeout --preserve-status 24h                               \
        ${WORKDIR}/MOpt-AFL/MOpt-AFL\ V1.0/afl-fuzz -L 0 -m none    \
            -i ${WORKDIR}/seeds -o ${WORKDIR}/mopt-out-$I           \
            -- ${AFL_TARGET} ${TARGET_OPTS} > ${WORKDIR}/mopt-$I.log 2>&1 &
    sleep 2
done

# datAFLow access fuzz
for I in $(seq 1 5); do
    LD_LIBRARY_PATH=${DATAFLOW_ACCESS_LD}:${LD_LIBRARY_PATH}                    \
    nohup /usr/bin/time --verbose --output=${WORKDIR}/datAFLow-access-$I.time   \
        timeout --preserve-status 24h                                           \
        ${WORKDIR}/AFL/afl-fuzz -m none                                         \
            -i ${WORKDIR}/seeds -o ${WORKDIR}/datAFLow-access-out-$I --         \
            ${DATAFLOW_ACCESS_TARGET} ${TARGET_OPTS} > ${WORKDIR}/datAFLow-access-$I.log 2>&1 &
    sleep 2
done

# datAFLow access MOpt fuzz
for I in $(seq 1 5); do
    LD_LIBRARY_PATH=${DATAFLOW_ACCESS_LD}:${LD_LIBRARY_PATH}                        \
    nohup /usr/bin/time --verbose --output=${WORKDIR}/datAFLow-access-mopt-$I.time  \
        timeout --preserve-status 24h                                               \
        ${WORKDIR}/MOpt-AFL/MOpt-AFL\ V1.0/afl-fuzz -L 0 -m none                    \
            -i ${WORKDIR}/seeds -o ${WORKDIR}/datAFLow-access-mopt-out-$I --        \
            ${DATAFLOW_ACCESS_TARGET} ${TARGET_OPTS} > ${WORKDIR}/datAFLow-access-mopt-$I.log 2>&1 &
    sleep 2
done

# datAFLow access + index fuzz
for I in $(seq 1 5); do
    LD_LIBRARY_PATH=${DATAFLOW_ACCESS_IDX_LD}:${LD_LIBRARY_PATH}                    \
    nohup /usr/bin/time --verbose --output=${WORKDIR}/datAFLow-access-idx-$I.time   \
        timeout --preserve-status 24h                                               \
        ${WORKDIR}/AFL/afl-fuzz -m none                                             \
            -i ${WORKDIR}/seeds -o ${WORKDIR}/datAFLow-access-idx-out-$I --         \
            ${DATAFLOW_ACCESS_TARGET} ${TARGET_OPTS} > ${WORKDIR}/datAFLow-access-idx-$I.log 2>&1 &
    sleep 2
done

# datAFLow access + index MOpt fuzz
for I in $(seq 1 5); do
    LD_LIBRARY_PATH=${DATAFLOW_ACCESS_IDX_LD}:${LD_LIBRARY_PATH}                        \
    nohup /usr/bin/time --verbose --output=${WORKDIR}/datAFLow-access-idx-mopt-$I.time  \
        timeout --preserve-status 24h                                                   \
        ${WORKDIR}/MOpt-AFL/MOpt-AFL\ V1.0/afl-fuzz -L 0 -m none                        \
            -i ${WORKDIR}/seeds -o ${WORKDIR}/datAFLow-access-idx-mopt-out-$I --        \
            ${DATAFLOW_ACCESS_IDX_TARGET} ${TARGET_OPTS} > ${WORKDIR}/datAFLow-access-idx-mopt-$I.log 2>&1 &
    sleep 2
done
