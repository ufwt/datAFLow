FROM dataflow/base
MAINTAINER adrian.herrera02@gmail.com

RUN curl -L https://github.com/westes/flex/files/981163/flex-2.6.4.tar.gz | tar xzf -

ENV TARGET "flex"
ENV TARGET_SRC "flex-2.6.4"

ENV BUILD_CMD_AFL   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install"
ENV BUILD_CMD_AFL_DBG   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install"

ENV BUILD_CMD_DATAFLOW_TAGS \
    "./configure --disable-shared --enable-static &&                             \
    make clean && make -j1"

ENV BUILD_CMD_DATAFLOW_ACCESS   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_DBG   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install"

ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS_DBG   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_DBG   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS_DBG   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"

ADD whitelist.txt ${WORKDIR}/${TARGET_SRC}
RUN ${WORKDIR}/build.sh

# Seeds
RUN find ${TARGET_SRC} -name '*.l' -type f -exec cp {} seeds/ \;

CMD ["bash"]
