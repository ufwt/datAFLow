FROM dataflow/base
MAINTAINER adrian.herrera02@gmail.com

RUN curl -L https://www.nasm.us/pub/nasm/releasebuilds/2.14.02/nasm-2.14.02.tar.gz | tar xzvf -

# Build AFL target
RUN mkdir nasm-afl &&                               \
    cd nasm-2.14.02 &&                              \
    export CC="${AFL_PATH}/afl-clang-fast" &&       \
    export CXX="${AFL_PATH}/afl-clang-fast++" &&    \
    export CFLAGS="-fsanitize=address" &&           \
    export CXXFLAGS="-fsanitize=address" &&         \
    ./configure --prefix=${WORKDIR}/nasm-afl &&     \
    make clean && make -j && make install

# Collect tags
ADD whitelist.txt nasm-2.14.02/
RUN cd nasm-2.14.02 &&                                                                  \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                               \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" &&         \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-collect-tag-sites" &&      \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-collect-tag-sites++" &&   \
    export FUZZALLOC_WHITELIST="$(pwd)/whitelist.txt" &&                                \
    export FUZZALLOC_TAG_LOG="$(pwd)/nasm-2.14.02-tag-sites.csv" &&                     \
    ./configure &&                                                                      \
    make clean && make -j1
RUN cd nasm-2.14.02 &&                                              \
    python3 ${WORKDIR}/datAFLow/scripts/remove_duplicate_lines.py   \
        nasm-2.14.02-tag-sites.csv > temp.csv &&                    \
    mv temp.csv nasm-2.14.02-tag-sites.csv

# Build datAFLow memory access target
RUN mkdir nasm-datAFLow-access &&                                               \
    cd nasm-2.14.02 &&                                                          \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/nasm-2.14.02-tag-sites.csv" &&             \
    export FUZZALLOC_SENSITIVITY="mem-access" &&                                \
    ./configure --prefix=${WORKDIR}/nasm-datAFLow-access &&                     \
    make clean && make -j && make install
RUN cd nasm-datAFLow-access &&                                  \
    find bin -type f -executable -exec                          \
        python3 ${WORKDIR}/datAFLow/scripts/prelink_binary.py   \
        --in-place --out-dir={}_prelink {} \;

# Build datAFLow memory access + index target
RUN mkdir nasm-datAFLow-access-idx &&                                           \
    cd nasm-2.14.02 &&                                                          \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/nasm-2.14.02-tag-sites.csv" &&             \
    export FUZZALLOC_SENSITIVITY="mem-access-idx" &&                            \
    ./configure --prefix=${WORKDIR}/nasm-datAFLow-access-idx &&                 \
    make clean && make -j && make install
RUN cd nasm-datAFLow-access-idx &&                              \
    find bin -type f -executable -exec                          \
        python3 ${WORKDIR}/datAFLow/scripts/prelink_binary.py   \
        --in-place --out-dir={}_prelink {} \;

# Seeds
RUN mkdir seeds
RUN cp -n $(find nasm-2.14.02 -type f -name '*.asm' -exec realpath {} \+) seeds

ADD fuzz-config.sh ${WORKDIR}
CMD ["bash"]
