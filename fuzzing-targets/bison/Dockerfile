FROM dataflow/base-image
MAINTAINER adrian.herrera02@gmail.com

RUN apt update &&   \
    apt install -y flex

ADD https://ftp.gnu.org/gnu/bison/bison-3.5.tar.gz .
RUN tar xf bison-3.5.tar.gz &&  \
    rm bison-3.5.tar.gz

# Build AFL target
RUN mkdir bison-afl &&                              \
    cd bison-3.5 &&                                 \
    export CC="${AFL_PATH}/afl-clang-fast" &&       \
    export CXX="${AFL_PATH}/afl-clang-fast++" &&    \
    export CFLAGS="-fsanitize=address" &&           \
    export CXXFLAGS="-fsanitize=address" &&         \
    ./configure --prefix=${WORKDIR}/bison-afl &&    \
    make clean && make -j && make install

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

# Build datAFLow target
RUN mkdir bison-datAFLow &&                                                     \
    cd bison-3.5 &&                                                             \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/bison-3.5-tag-sites.csv" &&                \
    ./configure --prefix=${WORKDIR}/bison-datAFLow &&   \
    make clean && make -j && make install

# Prelink
RUN cd bison-datAFLow &&                                    \
    python3 ${WORKDIR}/datAFLow/scripts/prelink_binary.py   \
        --in-place --out-dir="$(pwd)/prelink" bin/bison
