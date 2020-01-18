#
# An intermediate container for cloning the (private) datAFLow repo
#

FROM ubuntu:18.04 as datAFLow-intermediate
ARG SSH_PRIVATE_KEY

ENV DEBIAN_FRONTEND noninteractive
ENV WORKDIR /root
WORKDIR ${WORKDIR}

RUN apt update &&       \
    apt install -y git

RUN mkdir ${WORKDIR}/.ssh &&    \
    chmod 700 ${WORKDIR}/.ssh
RUN echo "${SSH_PRIVATE_KEY}" > ${WORKDIR}/.ssh/id_rsa
RUN chmod 600 ${WORKDIR}/.ssh/id_rsa

RUN touch ${WORKDIR}/.ssh/known_hosts
RUN ssh-keyscan github.com >> ${WORKDIR}/.ssh/known_hosts

RUN git clone --depth=1 git@github.com:HexHive/datAFLow.git

#
# The actual container
#

FROM ubuntu:18.04
MAINTAINER adrian.herrera02@gmail.com

ENV DEBIAN_FRONTEND noninteractive

# Install required packages
RUN apt update &&                                               \
    apt install -y build-essential git autogen autoconf texinfo \
        automake tzdata libtool cmake ninja-build pkg-config    \
        curl flex bison python python3 python3-dev python3-pip  \
        time prelink patchelf &&                                \
    apt autoremove -y

ENV WORKDIR /root
WORKDIR ${WORKDIR}

# Install Go
RUN mkdir goroot gopath
RUN curl https://dl.google.com/go/go1.13.6.linux-amd64.tar.gz | \
    tar xzvf - -C goroot --strip-components=1
ENV GOROOT ${WORKDIR}/goroot
ENV GOPATH ${WORKDIR}/gopath
ENV PATH ${GOROOT}/bin:${GOPATH}/bin:${PATH}

# wllvm
RUN go get github.com/SRI-CSL/gllvm/cmd/...

# binutils
RUN git clone --depth 1 git://sourceware.org/git/binutils-gdb.git binutils
RUN mkdir binutils/build &&                                 \
    cd binutils/build &&                                    \
    ../configure --enable-gold=default --enable-plugins &&  \
    make clean && make -j all-gold

# Install binutils as root
RUN make -C binutils/build install-gold

# LLVM
RUN git clone --depth=1 --branch release/7.x --single-branch https://github.com/llvm/llvm-project.git
RUN mkdir llvm-project/build &&                             \
    cd llvm-project/build &&                                \
    cmake ../llvm -DCMAKE_BUILD_TYPE=Release                \
        -DLLVM_ENABLE_PROJECTS="clang;compiler-rt"          \
        -DLLVM_BUILD_EXAMPLES=Off                           \
        -DLLVM_INCLUDE_EXAMPLES=Off                         \
        -DLLVM_TARGETS_TO_BUILD="X86"                       \
        -DLLVM_BINUTILS_INCDIR=${WORKDIR}/binutils/include  \
        -GNinja
RUN cmake --build llvm-project/build

# Install LLVM as root
RUN cmake --build llvm-project/build --target install
RUN ln -s /usr/local/lib/libLTO.so /usr/lib/bfd-plugins/ && \
    ln -s /usr/local/lib/LLVMgold.so /usr/lib/bfd-plugins/

RUN rm -rf llvm-project/build
ENV CC clang
ENV CXX clang++

# Fuzzing seeds
RUN git clone --depth=1 https://github.com/MozillaSecurity/fuzzdata

# AFL
RUN git clone --depth=1 https://github.com/google/AFL.git
RUN cd AFL &&                   \
    make clean && make -j &&    \
    make -j -C llvm_mode
ENV AFL_PATH ${WORKDIR}/AFL
ENV AFL_NO_UI 1

# AFL MOpt
RUN git clone --depth=1 https://github.com/puppet-meteor/MOpt-AFL
RUN cd MOpt-AFL/MOpt-AFL\ V1.0 &&   \
    make clean && make -j &&        \
    make -j -C llvm_mode

# libFuzzer - AFL
RUN export CC="${AFL_PATH}/afl-clang-fast" &&                                                           \
    export CXX="${AFL_PATH}/afl-clang-fast++" &&                                                        \
    ${CC} -O2 -c ${WORKDIR}/llvm-project/compiler-rt/lib/fuzzer/standalone/StandaloneFuzzTargetMain.c &&\
    ar rc ${AFL_PATH}/libaflfuzz.a StandaloneFuzzTargetMain.o &&                                        \
    rm StandaloneFuzzTargetMain.o

# datAFLow
COPY --from=datAFLow-intermediate ${WORKDIR}/datAFLow ${WORKDIR}/datAFLow
RUN mkdir fuzzalloc-debug &&            \
    cd fuzzalloc-debug &&               \
    cmake -DFUZZALLOC_USE_LOCKS=False   \
        -DAFL_INSTRUMENT=On             \
        -DCMAKE_BUILD_TYPE=Debug        \
        ${WORKDIR}/datAFLow/fuzzalloc &&\
    make -j
RUN mkdir fuzzalloc-release &&          \
    cd fuzzalloc-release &&             \
    cmake -DFUZZALLOC_USE_LOCKS=False   \
        -DAFL_INSTRUMENT=On             \
        -DCMAKE_BUILD_TYPE=Release      \
        ${WORKDIR}/datAFLow/fuzzalloc &&\
    make -j
RUN pip3 install -r ${WORKDIR}/datAFLow/scripts/requirements.txt
ENV LD_LIBRARY_PATH ${WORKDIR}/fuzzalloc-release/src/runtime/malloc:${LD_LIBRARY_PATH}
ENV ASAN_OPTIONS "detect_leaks=0:abort_on_error=1:symbolize=0"

# Rebuild LLVM to support datAFLow
RUN cd llvm-project &&                                                  \
    ${WORKDIR}/datAFLow/fuzzalloc/scripts/update_clang_src.sh &&        \
    ${WORKDIR}/datAFLow/fuzzalloc/scripts/update_compiler_rt_src.sh &&  \
    ${WORKDIR}/datAFLow/fuzzalloc/scripts/update_llvm_src.sh
RUN mkdir llvm-datAFLow &&                                  \
    mkdir llvm-project/build &&                             \
    cd llvm-project/build &&                                \
    cmake ../llvm -DCMAKE_BUILD_TYPE=Release                \
        -DLLVM_ENABLE_PROJECTS="clang;compiler-rt"          \
        -DLLVM_BUILD_EXAMPLES=Off                           \
        -DLLVM_INCLUDE_EXAMPLES=Off                         \
        -DLLVM_TARGETS_TO_BUILD="X86"                       \
        -DLLVM_BINUTILS_INCDIR=${WORKDIR}/binutils/include  \
        -DFUZZALLOC_ASAN=On                                 \
        -DLIBFUZZALLOC_PATH=${WORKDIR}/fuzzalloc-release/src/runtime/malloc/libfuzzalloc.so \
        -DCMAKE_INSTALL_PREFIX=${WORKDIR}/llvm-datAFLow     \
        -GNinja
RUN cmake --build llvm-project/build &&                 \
    cmake --build llvm-project/build --target install
RUN rm -rf llvm-project
