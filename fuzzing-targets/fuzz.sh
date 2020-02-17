#!/bin/bash -ex

if [ "$#" -ne 1 ]; then
    echo "usage: $0 TARGET"
    exit 1
fi

SCRIPT_DIR=$(dirname $(realpath -s $0))
TARGET=$1
TIMEOUT=86400
JOBS=50
SEEDS="${TARGET}/seeds"

export AFL_NO_UI=1
. ${SCRIPT_DIR}/${TARGET}/fuzz-config.sh

# Determine if there is a dictionary to use
if [ -f "${SCRIPT_DIR}/${TARGET}/${TARGET}.dict" ]; then
    AFL_DICT="-x ${SCRIPT_DIR}/${TARGET}/${TARGET}.dict"
else
    AFL_DICT=
fi

# Make the target directory
mkdir -p ${TARGET}

# Start the Docker container so that we can extract everything from it
docker run --name dataflow-${TARGET} dataflow/${TARGET}

docker cp dataflow-${TARGET}:/root/seeds ${TARGET}/

for BUILD in afl                            \
             angora-fast                    \
             angora-track                   \
             datAFLow-access                \
             datAFLow-access-heapify-structs\
             datAFLow-access-idx            \
             datAFLow-access-idx-heapify-structs; do
    # Extract target from Docker container
    docker cp dataflow-${TARGET}:/root/${TARGET}-${BUILD} ${TARGET}/
    if [ $? -ne 0 ]; then
        continue
    fi

    EXE_PATH="${TARGET}/${TARGET}-${BUILD}/bin/${EXE}"
    DEPS_DIR="${EXE_PATH}_deps"

    # Fix the interpreter
    patchelf --set-interpreter "${DEPS_DIR}/ld-linux-x86-64.so.2" ${EXE_PATH}

    # AFL fuzz
    for I in $(seq 1 5); do
        LD_LIBRARY_PATH=${DEPS_DIR}:${LD_LIBRARY_PATH}                          \
        sem --timeout ${TIMEOUT} --jobs ${JOBS} --id "fuzz-${EXE}" -u           \
            --halt now,fail=1                                                   \
        /usr/bin/time --verbose --output="${TARGET}/${BUILD}-${I}.time"         \
        AFL/afl-fuzz -m none -i ${SEEDS} -o "${TARGET}/${BUILD}-out-${I}"       \
            ${AFL_DICT} --                                                      \
            ${EXE_PATH} ${EXE_OPTS} > "${TARGET}/${BUILD}-${I}.log" 2>&1
        sleep 2
    done

    # MOpt fuzz
    for I in $(seq 1 5); do
        LD_LIBRARY_PATH=${DEPS_DIR}:${LD_LIBRARY_PATH}                          \
        sem --timeout ${TIMEOUT} --jobs ${JOBS} --id "fuzz-${EXE}" -u -q        \
            --halt now,fail=1                                                   \
        /usr/bin/time --verbose --output="${TARGET}/mopt-${BUILD}-${I}.time"    \
        "MOpt-AFL/MOpt-AFL V1.0/afl-fuzz" -m none -L 0                          \
            -i ${SEEDS} -o "${TARGET}/mopt-${BUILD}-out-${I}"                   \
            ${AFL_DICT} --                                                      \
            ${EXE_PATH} ${EXE_OPTS} > "${TARGET}/mopt-${BUILD}-${I}.log" 2>&1
        sleep 2
    done
done

# Wait for campaigns to finish
sem --wait --id "fuzz-${EXE}"

# Remove the Docker container
docker stop dataflow-${TARGET}
docker rm -f dataflow-${TARGET}
