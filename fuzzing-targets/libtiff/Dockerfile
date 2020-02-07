FROM dataflow/base
MAINTAINER adrian.herrera02@gmail.com

RUN curl -L https://download.osgeo.org/libtiff/tiff-4.1.0.tar.gz | tar xzf -

ENV TARGET "libtiff"
ENV TARGET_SRC "tiff-4.1.0"

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
RUN find ${TARGET_SRC} -name '*.tiff' -type f -exec cp {} seeds/ \;
RUN cp ${WORKDIR}/fuzzdata/samples/tiff/* seeds/

CMD ["bash"]
