FROM dataflow/base-image
MAINTAINER adrian.herrera02@gmail.com

ADD https://www.nasm.us/pub/nasm/releasebuilds/2.14.02/nasm-2.14.02.tar.gz .
RUN tar xf nasm-2.14.02.tar.gz &&   \
    rm nasm-2.14.02.tar.gz

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

# Build datAFLow target
RUN mkdir nasm-datAFLow &&                                                      \
    cd nasm-2.14.02 &&                                                          \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/nasm-2.14.02-tag-sites.csv" &&             \
    ./configure --prefix=${WORKDIR}/nasm-datAFLow &&                            \
    make clean && make -j && make install

# Prelink
RUN cd nasm-datAFLow &&                                         \
    find bin -type f -exec                                      \
        python3 ${WORKDIR}/datAFLow/scripts/prelink_binary.py   \
        --in-place --out-dir={}_prelink {} \;
