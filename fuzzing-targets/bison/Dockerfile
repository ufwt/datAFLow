FROM dataflow/base
MAINTAINER adrian.herrera02@gmail.com

RUN curl -L https://ftp.gnu.org/gnu/bison/bison-3.5.tar.gz | tar xzvf -

# Build AFL target
RUN mkdir bison-afl &&                              \
    cd bison-3.5 &&                                 \
    export CC="${AFL_PATH}/afl-clang-fast" &&       \
    export CXX="${AFL_PATH}/afl-clang-fast++" &&    \
    export CFLAGS="-fsanitize=address" &&           \
    export CXXFLAGS="-fsanitize=address" &&         \
    ./configure --prefix=${WORKDIR}/bison-afl &&    \
    make clean && make -j && make install
RUN find bison-afl/bin -type f -executable                              \
        -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/get_libs.py  \
        --out-dir={}_deps {} \;

# Build AFL debug target
RUN mkdir bison-afl-debug &&                                    \
    cd bison-3.5 &&                                             \
    export CC="${AFL_PATH}/afl-clang-fast" &&                   \
    export CXX="${AFL_PATH}/afl-clang-fast++" &&                \
    export AFL_DEBUG="1" &&                                     \
    ./configure --prefix=${WORKDIR}/bison-afl-debug &&          \
    make clean && make -j && make install
RUN find bison-afl-debug/bin -type f -executable                        \
        -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/get_libs.py  \
        --out-dir={}_deps {} \;

# Collect tags
ADD whitelist.txt bison-3.5/
RUN cd bison-3.5 &&                                                                     \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                               \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" &&         \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-collect-tag-sites" &&      \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-collect-tag-sites++" &&   \
    export FUZZALLOC_WHITELIST="$(pwd)/whitelist.txt" &&                                \
    export FUZZALLOC_TAG_LOG="$(pwd)/bison-3.5-tag-sites.csv" &&                        \
    ./configure &&                                                                      \
    make clean && make -j1
RUN cd bison-3.5 &&                                                 \
    python3 ${WORKDIR}/datAFLow/scripts/remove_duplicate_lines.py   \
        bison-3.5-tag-sites.csv > temp.csv &&                       \
    mv temp.csv bison-3.5-tag-sites.csv

# Build datAFLow memory access target
RUN mkdir bison-datAFLow-access &&                                              \
    cd bison-3.5 &&                                                             \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/bison-3.5-tag-sites.csv" &&                \
    export FUZZALLOC_SENSITIVITY="mem-access" &&                                \
    ./configure --prefix=${WORKDIR}/bison-datAFLow-access &&                    \
    make clean && make -j && make install
RUN find bison-datAFLow-access/bin -type f -executable                          \
        -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py    \
        --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access debug target
RUN mkdir bison-datAFLow-access-debug &&                                        \
    cd bison-3.5 &&                                                             \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC=gclang &&                                                         \
    export CXX=gclang++ &&                                                      \
    export LLVM_CC_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&       \
    export LLVM_CXX_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&    \
    export FUZZALLOC_TAG_LOG="$(pwd)/bison-3.5-tag-sites.csv" &&                \
    export FUZZALLOC_SENSITIVITY="mem-access" &&                                \
    export FUZZALLOC_DEBUG_INSTRUMENT="1" &&                                    \
    ./configure --prefix=${WORKDIR}/bison-datAFLow-access-debug &&              \
    make clean && make -j && make install
RUN find bison-datAFLow-access-debug/bin -type f -executable    \
        -exec get-bc {} \;
RUN find bison-datAFLow-access-debug/bin -type f -executable                    \
        -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py    \
        --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + heapify structs target
RUN mkdir bison-datAFLow-access-heapify-structs &&                              \
    cd bison-3.5 &&                                                             \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/bison-3.5-tag-sites.csv" &&                \
    export FUZZALLOC_HEAPIFY_STRUCTS="1" &&                                     \
    export FUZZALLOC_SENSITIVITY="mem-access" &&                                \
    ./configure --prefix=${WORKDIR}/bison-datAFLow-access-heapify-structs &&    \
    make clean && make -j && make install
RUN find bison-datAFLow-access-heapify-structs/bin -type f -executable          \
        -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py    \
        --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + heapify structs debug target
RUN mkdir bison-datAFLow-access-heapify-structs-debug &&                        \
    cd bison-3.5 &&                                                             \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC=gclang &&                                                         \
    export CXX=gclang++ &&                                                      \
    export LLVM_CC_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&       \
    export LLVM_CXX_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&    \
    export FUZZALLOC_TAG_LOG="$(pwd)/bison-3.5-tag-sites.csv" &&                \
    export FUZZALLOC_HEAPIFY_STRUCTS="1" &&                                     \
    export FUZZALLOC_SENSITIVITY="mem-access" &&                                \
    export FUZZALLOC_DEBUG_INSTRUMENT="1" &&                                    \
    ./configure --prefix=${WORKDIR}/bison-datAFLow-access-heapify-structs-debug &&          \
    make clean && make -j && make install
RUN find bison-datAFLow-access-heapify-structs-debug/bin -type f -executable    \
        -exec get-bc {} \;
RUN find bison-datAFLow-access-heapify-structs-debug/bin -type f -executable    \
        -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py    \
        --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + index target
RUN mkdir bison-datAFLow-access-idx &&                                          \
    cd bison-3.5 &&                                                             \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/bison-3.5-tag-sites.csv" &&                \
    export FUZZALLOC_SENSITIVITY="mem-access-idx" &&                            \
    ./configure --prefix=${WORKDIR}/bison-datAFLow-access-idx &&                \
    make clean && make -j && make install
RUN find bison-datAFLow-access-idx/bin -type f -executable                      \
        -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py    \
        --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + index + heapify structs target
RUN mkdir bison-datAFLow-access-idx-heapify-structs &&                          \
    cd bison-3.5 &&                                                             \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/bison-3.5-tag-sites.csv" &&                \
    export FUZZALLOC_HEAPIFY_STRUCTS="1" &&                                     \
    export FUZZALLOC_SENSITIVITY="mem-access-idx" &&                            \
    ./configure --prefix=${WORKDIR}/bison-datAFLow-access-idx-heapify-structs &&\
    make clean && make -j && make install
RUN find bison-datAFLow-access-idx-heapify-structs/bin -type f -executable      \
        -exec python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py    \
        --in-place --out-dir={}_deps {} \;

# Seeds
RUN mkdir seeds
RUN cp -n $(find bison-3.5 -type f -name '*.y' -exec realpath {} \+) seeds

CMD ["bash"]
