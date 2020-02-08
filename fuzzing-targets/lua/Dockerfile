FROM dataflow/base
MAINTAINER adrian.herrera02@gmail.com

RUN apt update &&                   \
    apt install -y libreadline-dev

RUN curl -L https://www.lua.org/ftp/lua-5.3.5.tar.gz | tar xzvf -

# Add Makefiles
ADD Makefile ${WORKDIR}/lua-5.3.5/
ADD src/Makefile ${WORKDIR}/lua-5.3.5/src/

ENV TARGET "lua"
ENV TARGET_SRC "lua-5.3.5"

ENV BUILD_CMD_AFL   \
    "export INSTALL_TOP=\${INSTALL_PREFIX} &&       \
    make clean && make linux -j && make install"
ENV BUILD_CMD_AFL_DBG  \
    "export INSTALL_TOP=\${INSTALL_PREFIX} &&       \
    make clean && make linux -j && make install"

ENV BUILD_CMD_DATAFLOW_TAGS \
    "make clean && make linux -j1"

ENV BUILD_CMD_DATAFLOW_ACCESS   \
    "export INSTALL_TOP=\${INSTALL_PREFIX} &&       \
    make clean && make linux -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_DBG   \
    "export INSTALL_TOP=\${INSTALL_PREFIX} &&       \
    make clean && make linux -j && make install"

ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS   \
    "export INSTALL_TOP=\${INSTALL_PREFIX} &&   \
    make clean && make linux -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS_DBG   \
    "export INSTALL_TOP=\${INSTALL_PREFIX} &&       \
    make clean && make linux -j && make install"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX   \
    "export INSTALL_TOP=\${INSTALL_PREFIX} &&       \
    make clean && make linux -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_DBG   \
    "export INSTALL_TOP=\${INSTALL_PREFIX} &&       \
    make clean && make linux -j && make install"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS   \
    "export INSTALL_TOP=\${INSTALL_PREFIX} &&       \
    make clean && make linux -j && make install"
ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS_DBG   \
    "export INSTALL_TOP=\${INSTALL_PREFIX} &&           \
    make clean && make linux -j && make install"

ADD whitelist.txt ${WORKDIR}/${TARGET_SRC}
RUN ${WORKDIR}/build.sh

# Seeds
RUN echo "" > seeds/empty

CMD ["bash"]
