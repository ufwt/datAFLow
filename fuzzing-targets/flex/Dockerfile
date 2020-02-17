FROM dataflow/base
MAINTAINER adrian.herrera02@gmail.com

RUN apt-get install -y autopoint
RUN git clone --depth 1 https://github.com/westes/flex

ENV TARGET "flex"
ENV TARGET_SRC "flex"

ENV BUILD_CMD_AFL   \
    "./autogen.sh && ./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install"
ENV BUILD_CMD_AFL_DBG   \
    "./autogen.sh && ./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install"

ENV BUILD_CMD_ANGORA   \
    "./autogen.sh && ./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install"

ENV BUILD_CMD_DATAFLOW_TAGS \
    "./autogen.sh && ./configure --disable-shared --enable-static &&                             \
    make clean && make -j1"

ENV BUILD_CMD_DATAFLOW_ACCESS   \
    "./autogen.sh && ./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_DBG   \
    "./autogen.sh && ./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install"

ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS   \
    "./autogen.sh && ./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS_DBG   \
    "./autogen.sh && ./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX   \
    "./autogen.sh && ./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_DBG   \
    "./autogen.sh && ./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS   \
    "./autogen.sh && ./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS_DBG   \
    "./autogen.sh && ./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install"

ADD whitelist.txt ${WORKDIR}/${TARGET_SRC}
RUN ${WORKDIR}/build.sh

# Seeds
RUN find ${TARGET_SRC} -name '*.l' -type f -exec cp {} seeds/ \;

CMD ["bash"]
