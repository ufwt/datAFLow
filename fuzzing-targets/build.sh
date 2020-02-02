#!/bin/bash -x

# Build AFL target
mkdir ${TARGET}-afl
(cd ${TARGET_SRC} &&                                                        \
    export INSTALL_PREFIX="${WORKDIR}/${TARGET}-afl" &&                     \
    export CC="${AFL_PATH}/afl-clang-fast" &&                               \
    export CXX="${AFL_PATH}/afl-clang-fast++" &&                            \
    export CFLAGS="-fsanitize=address" &&                                   \
    export CXXFLAGS="-fsanitize=address" &&                                 \
    eval ${BUILD_CMD_AFL})

find ${TARGET}-afl/bin -type f -executable                                  \
    -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/get_libs.py          \
    --out-dir={}_deps {} \;

# Build AFL debug target
mkdir ${TARGET}-afl-debug
(cd ${TARGET_SRC} &&                                                        \
    export INSTALL_PREFIX="${WORKDIR}/${TARGET}-afl-debug" &&               \
    export CC=gclang &&                                                     \
    export CXX=gclang++ &&                                                  \
    export LLVM_CC_NAME="${AFL_PATH}/afl-clang-fast" &&                     \
    export LLVM_CXX_NAME="${AFL_PATH}/afl-clang-fast++" &&                  \
    export AFL_DEBUG="1" &&                                                 \
    eval ${BUILD_CMD_AFL_DBG})

find ${TARGET}-afl-debug/bin -type f -executable                            \
    -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/get_libs.py          \
    --out-dir={}_deps {} \;

# Collect tags
(cd ${TARGET_SRC} &&                                                                    \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                               \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" &&         \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-collect-tag-sites" &&      \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-collect-tag-sites++" &&   \
    export FUZZALLOC_WHITELIST="$(pwd)/whitelist.txt" &&                                \
    export FUZZALLOC_TAG_LOG="$(pwd)/${TARGET}-tag-sites.csv" &&                        \
    eval ${BUILD_CMD_DATAFLOW_TAGS})
python3 ${WORKDIR}/datAFLow/scripts/remove_duplicate_lines.py               \
    ${TARGET_SRC}/${TARGET}-tag-sites.csv > ${TARGET_SRC}/temp.csv &&       \
mv ${TARGET_SRC}/temp.csv ${TARGET_SRC}/${TARGET}-tag-sites.csv

# Build datAFLow memory access target
mkdir ${TARGET}-datAFLow-access
(cd ${TARGET_SRC} &&                                                            \
    export INSTALL_PREFIX="${WORKDIR}/${TARGET}-datAFLow-access" &&             \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/${TARGET}-tag-sites.csv" &&                \
    export FUZZALLOC_SENSITIVITY="mem-access" &&                                \
    eval ${BUILD_CMD_DATAFLOW_ACCESS})
find ${TARGET}-datAFLow-access/bin -type f -executable                          \
        -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py    \
        --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access debug target
mkdir ${TARGET}-datAFLow-access-debug
(cd ${TARGET_SRC} &&                                                            \
    export INSTALL_PREFIX="${WORKDIR}/${TARGET}-datAFLow-access-debug" &&       \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC=gclang &&                                                         \
    export CXX=gclang++ &&                                                      \
    export LLVM_CC_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&       \
    export LLVM_CXX_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&    \
    export FUZZALLOC_TAG_LOG="$(pwd)/${TARGET}-tag-sites.csv" &&                \
    export FUZZALLOC_SENSITIVITY="mem-access" &&                                \
    export FUZZALLOC_DEBUG_INSTRUMENT="1" &&                                    \
    eval ${BUILD_CMD_DATAFLOW_ACCESS_DBG})
find ${TARGET}-datAFLow-access-debug/bin -type f -executable -exec          \
    get-bc {} \;
find bin ${TARGET}-datAFLow-access-debug/bin -type f -executable            \
    -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py    \
    --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + heapify structs target
mkdir ${TARGET}-datAFLow-access-heapify-structs
(cd ${TARGET_SRC} &&                                                            \
    export INSTALL_PREFIX="${WORKDIR}/${TARGET}-datAFLow-access-heapify-structs" && \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/${TARGET}-tag-sites.csv" &&                \
    export FUZZALLOC_HEAPIFY_STRUCTS="1" &&                                     \
    export FUZZALLOC_SENSITIVITY="mem-access" &&                                \
    eval ${BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS})
find ${TARGET}-datAFLow-access-heapify-structs/bin -type f -executable      \
    -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py    \
    --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + heapify structs debug target
mkdir ${TARGET}-datAFLow-access-heapify-structs-debug
(cd ${TARGET_SRC} &&                                                            \
    export INSTALL_PREFIX="${WORKDIR}/${TARGET}-datAFLow-access-heapify-structs-debug" &&   \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC=gclang &&                                                         \
    export CXX=gclang++ &&                                                      \
    export LLVM_CC_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&       \
    export LLVM_CXX_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&    \
    export FUZZALLOC_TAG_LOG="$(pwd)/${TARGET}-3.5-tag-sites.csv" &&            \
    export FUZZALLOC_HEAPIFY_STRUCTS="1" &&                                     \
    export FUZZALLOC_SENSITIVITY="mem-access" &&                                \
    export FUZZALLOC_DEBUG_INSTRUMENT="1" &&                                    \
    eval ${BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS_DBG})
find ${TARGET}-datAFLow-access-heapify-structs-debug/bin -type f -executable    \
    -exec get-bc {} \;
find ${TARGET}-datAFLow-access-heapify-structs-debug/bin -type f -executable    \
    -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py        \
    --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + index target
mkdir ${TARGET}-datAFLow-access-idx
(cd ${TARGET_SRC} &&                                                            \
    export INSTALL_PREFIX="${WORKDIR}/${TARGET}-datAFLow-access-idx" &&         \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/${TARGET}-tag-sites.csv" &&                \
    export FUZZALLOC_SENSITIVITY="mem-access-idx" &&                            \
    eval ${BUILD_CMD_DATAFLOW_ACCESS_IDX})
find ${TARGET}-datAFLow-access-idx/bin -type f -executable                  \
    -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py    \
    --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + index debug target
mkdir ${TARGET}-datAFLow-access-idx-debug
(cd ${TARGET_SRC} &&                                                            \
    export INSTALL_PREFIX="${WORKDIR}/${TARGET}-datAFLow-access-idx-debug" &&   \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC=gclang &&                                                         \
    export CXX=gclang++ &&                                                      \
    export LLVM_CC_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&       \
    export LLVM_CXX_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&    \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/${TARGET}-tag-sites.csv" &&                \
    export FUZZALLOC_SENSITIVITY="mem-access-idx" &&                            \
    export FUZZALLOC_DEBUG_INSTRUMENT="1" &&                                    \
    eval ${BUILD_CMD_DATAFLOW_ACCESS_IDX_DBG})
find ${TARGET}-datAFLow-access-idx-debug/bin -type f -executable            \
    -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py    \
    --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + index + heapify structs target
mkdir ${TARGET}-datAFLow-access-idx-heapify-structs
(cd ${TARGET_SRC} &&                                                            \
    export INSTALL_PREFIX="${WORKDIR}/${TARGET}-datAFLow-access-idx-heapify-structs" && \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/${TARGET}-tag-sites.csv" &&                \
    export FUZZALLOC_HEAPIFY_STRUCTS="1" &&                                     \
    export FUZZALLOC_SENSITIVITY="mem-access-idx" &&                            \
    eval ${BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS})
find ${TARGET}-datAFLow-access-idx-heapify-structs/bin -type f -executable  \
    -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py    \
    --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + index + heapify structs debug target
mkdir ${TARGET}-datAFLow-access-idx-heapify-structs-debug
(cd ${TARGET_SRC} &&                                                            \
    export INSTALL_PREFIX="${WORKDIR}/${TARGET}-datAFLow-access-idx-heapify-structs-debug" &&\
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC=gclang &&                                                         \
    export CXX=gclang++ &&                                                      \
    export LLVM_CC_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&       \
    export LLVM_CXX_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&    \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/${TARGET}-tag-sites.csv" &&                \
    export FUZZALLOC_HEAPIFY_STRUCTS="1" &&                                     \
    export FUZZALLOC_SENSITIVITY="mem-access-idx" &&                            \
    export FUZZALLOC_DEBUG_INSTRUMENT="1" &&                                    \
    eval ${BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS_DBG})
find ${TARGET}-datAFLow-access-idx-heapify-structs-debug/bin -type f -executable\
    -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py        \
    --in-place --out-dir={}_deps {} \;
