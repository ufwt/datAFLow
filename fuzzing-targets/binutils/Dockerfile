FROM dataflow/base
MAINTAINER adrian.herrera02@gmail.com

RUN curl -L https://ftp.gnu.org/gnu/binutils/binutils-2.34.tar.xz | tar xJvf -

ENV TARGET "binutils"
ENV TARGET_SRC "binutils-2.34"

# Don't build the debug targets with gclang - for some reason they don't compile

ENV BUILD_CMD_AFL   \
    "./configure --prefix=\${INSTALL_PREFIX} &&     \
     make -j && make install && make clean distclean"

ENV BUILD_CMD_ANGORA   \
    "./configure --prefix=\${INSTALL_PREFIX} &&     \
     make -j && make install && make clean distclean"

ENV BUILD_CMD_DATAFLOW_TAGS \
    "./configure &&                                 \
     make -j1 && make clean distclean"

ENV BUILD_CMD_DATAFLOW_ACCESS   \
    "./configure --prefix=\${INSTALL_PREFIX} &&     \
     make -j && make install && make clean distclean"

ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS   \
    "./configure --prefix=\${INSTALL_PREFIX} && \
     make -j && make install && make clean distclean"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX   \
    "./configure --prefix=\${INSTALL_PREFIX} &&     \
     make -j && make install && make clean distclean"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS   \
    "./configure --prefix=\${INSTALL_PREFIX} &&     \
     make -j && make install && make clean distclean"

ADD whitelist.txt ${WORKDIR}/${TARGET_SRC}
RUN ${WORKDIR}/build.sh

CMD ["bash"]
