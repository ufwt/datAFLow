FROM dataflow/base
MAINTAINER adrian.herrera02@gmail.com

RUN apt update &&                                       \
    apt install -y libopenjp2-7-dev libpng-dev libz-dev \
        libtiff-dev libfreetype6-dev libfontconfig-dev

ADD https://poppler.freedesktop.org/poppler-0.84.0.tar.xz .
RUN tar xf poppler-0.84.0.tar.xz && \
    rm poppler-0.84.0.tar.xz

# Build AFL target
RUN mkdir poppler-afl &&                                    \
    mkdir poppler-0.84.0/build &&                           \
    cd poppler-0.84.0/build &&                              \
    export CC="${AFL_PATH}/afl-clang-fast" &&               \
    export CXX="${AFL_PATH}/afl-clang-fast++" &&            \
    export CFLAGS="-fsanitize=address" &&                   \
    export CXXFLAGS="-fsanitize=address" &&                 \
    cmake .. -DCMAKE_INSTALL_PREFIX=${WORKDIR}/poppler-afl  \
        -DBUILD_GTK_TESTS=Off -DBUILD_QT5_TESTS=Off         \
        -DBUILD_CPP_TESTS=Off -DENABLE_CPP=Off              \
        -DENABLE_SPLASH=Off -DENABLE_GTK_DOC=Off            \
        -DENABLE_QT5=Off -DBUILD_SHARED_LIBS=Off &&         \
    make clean && make -j && make install
RUN rm -rf poppler-0.84.0/build

# Collect tags
ADD whitelist.txt poppler-0.84.0/
RUN mkdir poppler-0.84.0/build &&                                                       \
    cd poppler-0.84.0/build &&                                                          \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                               \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" &&         \
    export CC="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-collect-tag-sites" &&      \
    export CXX="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-collect-tag-sites++" &&   \
    export FUZZALLOC_WHITELIST="$(pwd)/../whitelist.txt" &&                             \
    export FUZZALLOC_TAG_LOG="$(pwd)/../poppler-0.84.0-tag-sites.csv" &&                \
    cmake .. -DBUILD_GTK_TESTS=Off -DBUILD_QT5_TESTS=Off                                \
        -DBUILD_CPP_TESTS=Off -DENABLE_CPP=Off                                          \
        -DENABLE_SPLASH=Off -DENABLE_GTK_DOC=Off                                        \
        -DENABLE_QT5=Off -DBUILD_SHARED_LIBS=Off &&                                     \
    make clean && make -j1
RUN cd poppler-0.84.0 &&                                            \
    python3 ${WORKDIR}/datAFLow/scripts/remove_duplicate_lines.py   \
        poppler-0.84.0-tag-sites.csv > temp.csv &&                  \
    mv temp.csv poppler-0.84.0-tag-sites.csv
RUN rm -rf poppler-0.84.0/build

# Build datAFLow memory access target
#
# Note that we need to use gclang otherwise the CMake tests fail
RUN mkdir poppler-datAFLow-access &&                                            \
    mkdir poppler-0.84.0/build &&                                               \
    cd poppler-0.84.0/build &&                                                  \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC=gclang && export CXX=gclang++ &&                                  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/../poppler-0.84.0-tag-sites.csv" &&        \
    cmake .. -DCMAKE_INSTALL_PREFIX=${WORKDIR}/poppler-datAFLow-access          \
        -DBUILD_GTK_TESTS=Off -DBUILD_QT5_TESTS=Off                             \
        -DBUILD_CPP_TESTS=Off -DENABLE_CPP=Off                                  \
        -DENABLE_SPLASH=Off -DENABLE_GTK_DOC=Off                                \
        -DENABLE_QT5=Off -DBUILD_SHARED_LIBS=Off &&                             \
    export LLVM_CC_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&       \
    export LLVM_CXX_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&    \
    export FUZZALLOC_SENSITIVITY="mem-access" &&                                \
    make clean && make -j && make install
RUN rm -rf poppler-0.84.0/build
RUN cd poppler-datAFLow-access &&                               \
    find bin -type f -executable -exec                          \
        python3 ${WORKDIR}/datAFLow/scripts/prelink_binary.py   \
        --in-place --out-dir={}_prelink {} \;

# Build datAFLow memory access + index target
RUN mkdir poppler-datAFLow-access-idx &&                                        \
    mkdir poppler-0.84.0/build &&                                               \
    cd poppler-0.84.0/build &&                                                  \
    export PATH="${WORKDIR}/llvm-datAFLow/bin:${PATH}" &&                       \
    export LD_LIBRARY_PATH="${WORKDIR}/llvm-datAFLow/lib:${LD_LIBRARY_PATH}" && \
    export CC=gclang && export CXX=gclang++ &&                                  \
    export CFLAGS="-fsanitize=address" &&                                       \
    export CXXFLAGS="-fsanitize=address" &&                                     \
    export FUZZALLOC_TAG_LOG="$(pwd)/../poppler-0.84.0-tag-sites.csv" &&        \
    cmake .. -DCMAKE_INSTALL_PREFIX=${WORKDIR}/poppler-datAFLow-access-idx      \
        -DBUILD_GTK_TESTS=Off -DBUILD_QT5_TESTS=Off                             \
        -DBUILD_CPP_TESTS=Off -DENABLE_CPP=Off                                  \
        -DENABLE_SPLASH=Off -DENABLE_GTK_DOC=Off                                \
        -DENABLE_QT5=Off -DBUILD_SHARED_LIBS=Off &&                             \
    export LLVM_CC_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast" &&       \
    export LLVM_CXX_NAME="${WORKDIR}/fuzzalloc-debug/src/tools/dataflow-clang-fast++" &&    \
    export FUZZALLOC_SENSITIVITY="mem-access-idx" &&                            \
    make clean && make -j && make install
RUN rm -rf poppler-0.84.0/build
RUN cd poppler-datAFLow-access-idx &&                           \
    find bin -type f -executable -exec                          \
        python3 ${WORKDIR}/datAFLow/scripts/prelink_binary.py   \
        --in-place --out-dir={}_prelink {} \;

CMD ["bash"]
