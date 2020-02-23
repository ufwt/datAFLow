FROM dataflow/base
MAINTAINER adrian.herrera02@gmail.com

RUN curl -L https://www.nasm.us/pub/nasm/releasebuilds/2.14.02/nasm-2.14.02.tar.gz | tar xzvf -

ENV TARGET "nasm"
ENV TARGET_SRC "nasm-2.14.02"

ENV BUILD_CMD_AFL   \
    "./configure --prefix=\${INSTALL_PREFIX} &&                     \
    make clean && make -j && make install"
ENV BUILD_CMD_AFL_DBG "./configure --prefix=\${INSTALL_PREFIX} &&   \
    make clean && make -j && make install"

ENV BUILD_CMD_ANGORA "./configure --prefix=\${INSTALL_PREFIX} &&   \
    make clean && make -j && make install"

ENV BUILD_CMD_DATAFLOW_TAGS \
    "./configure &&                                 \
    make clean && make -j1"

ENV BUILD_CMD_DATAFLOW_ACCESS   \
    "./configure --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_DBG   \
    "./configure --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"

ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS   \
    "./configure --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS_DBG   \
    "./configure --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX   \
    "./configure --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_DBG   \
    "./configure --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS   \
    "./configure --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS_DBG   \
    "./configure --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"

ADD whitelist.txt ${WORKDIR}/${TARGET_SRC}
RUN ${WORKDIR}/build.sh

# Seeds
RUN cp -n $(find ${TARGET_SRC} -type f -name '*.asm' -exec realpath {} \+) seeds

CMD ["bash"]
