FROM dataflow/base
MAINTAINER adrian.herrera02@gmail.com

RUN apt update &&                   \
    apt install -y libreadline-dev

RUN curl -L https://www.lua.org/ftp/lua-5.3.5.tar.gz | tar xzvf -

# Add Makefiles
ADD Makefile ${WORKDIR}/lua-5.3.5/
ADD src/Makefile ${WORKDIR}/lua-5.3.5/src/

# Build AFL target
RUN mkdir lua-afl &&                                \
    cd lua-5.3.5 &&                                 \
    export INSTALL_TOP="${WORKDIR}/lua-afl" &&      \
    export CC="${AFL_PATH}/afl-clang-fast" &&       \
    export CXX="${AFL_PATH}/afl-clang-fast++" &&    \
    export CFLAGS="-fsanitize=address" &&           \
    export CXXFLAGS="-fsanitize=address" &&         \
    make clean && make linux -j && make install
RUN cd lua-afl &&                                               \
    find bin -type f -executable -exec                          \
        python3 ${WORKDIR}/datAFLow/scripts/bintools/get_libs.py\
        --out-dir={}_deps {} \;

# Collect tags
ADD whitelist.txt ${WORKDIR}/lua-5.3.5/
RUN cd lua-5.3.5 &&                                                                     \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                               \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" &&         \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-collect-tag-sites" &&      \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-collect-tag-sites++" &&   \
    export FUZZALLOC_WHITELIST="$(pwd)/whitelist.txt" &&                                \
    export FUZZALLOC_TAG_LOG="$(pwd)/lua-5.3.5-tag-sites.csv" &&                        \
    make clean && make linux -j1
RUN cd lua-5.3.5 &&                                                 \
    python3 ${WORKDIR}/datAFLow/scripts/remove_duplicate_lines.py   \
        lua-5.3.5-tag-sites.csv > temp.csv &&                       \
    mv temp.csv lua-5.3.5-tag-sites.csv

# Build datAFLow memory access target
RUN mkdir lua-datAFLow-access &&                                                \
    cd lua-5.3.5 &&                                                             \
    export INSTALL_TOP="${WORKDIR}/lua-datAFLow-access" &&                      \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/lua-5.3.5-tag-sites.csv" &&                \
    export FUZZALLOC_SENSITIVITY="mem-access" &&                                \
    make clean && make linux -j && make install
RUN cd lua-datAFLow-access &&                                           \
    find bin -type f -executable -exec                                  \
        python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py  \
        --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + heapify structs target
RUN mkdir lua-datAFLow-access-heapify-structs &&                                \
    cd lua-5.3.5 &&                                                             \
    export INSTALL_TOP="${WORKDIR}/lua-datAFLow-access-heapify-structs" &&      \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/lua-5.3.5-tag-sites.csv" &&                \
    export FUZZALLOC_HEAPIFY_STRUCTS="1" &&                                     \
    export FUZZALLOC_SENSITIVITY="mem-access" &&                                \
    make clean && make linux -j && make install
RUN cd lua-datAFLow-access-heapify-structs &&                           \
    find bin -type f -executable -exec                                  \
        python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py  \
        --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + index target
RUN mkdir lua-datAFLow-access-idx &&                                            \
    cd lua-5.3.5 &&                                                             \
    export INSTALL_TOP="${WORKDIR}/lua-datAFLow-access-idx" &&                  \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/lua-5.3.5-tag-sites.csv" &&                \
    export FUZZALLOC_SENSITIVITY="mem-access-idx" &&                            \
    make clean && make linux -j && make install
RUN cd lua-datAFLow-access-idx &&                                       \
    find bin -type f -executable -exec                                  \
        python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py  \
        --in-place --out-dir={}_deps {} \;

# Build datAFLow memory access + index + heapify structs target
RUN mkdir lua-datAFLow-access-idx-heapify-structs &&                            \
    cd lua-5.3.5 &&                                                             \
    export INSTALL_TOP="${WORKDIR}/lua-datAFLow-access-idx-heapify-structs" &&  \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&     \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/lua-5.3.5-tag-sites.csv" &&                \
    export FUZZALLOC_HEAPIFY_STRUCTS="1" &&                                     \
    export FUZZALLOC_SENSITIVITY="mem-access-idx" &&                            \
    make clean && make linux -j && make install
RUN cd lua-datAFLow-access-idx-heapify-structs &&                       \
    find bin -type f -executable -exec                                  \
        python3 ${WORKDIR}/datAFLow/scripts/bintools/prelink_binary.py  \
        --in-place --out-dir={}_deps {} \;

# Seeds
RUN mkdir seeds
RUN echo "" > seeds/empty

CMD ["bash"]
