FROM dataflow/base
MAINTAINER adrian.herrera02@gmail.com

RUN curl -L https://github.com/json-c/json-c/archive/json-c-0.13.1-20180305.tar.gz | tar xzf -

ENV TARGET "json-c"
ENV TARGET_SRC "json-c-json-c-0.13.1-20180305"

ENV BUILD_CMD_AFL   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install && \
    \${CXX} \${CXXFLAGS} fuzz/tokener_parse_ex_fuzzer.cc \${AFL_PATH}/libfuzzStandalone.a -o \${INSTALL_PREFIX}/bin/json-c -I. -L\${INSTALL_PREFIX}/lib -ljson-c"
ENV BUILD_CMD_AFL_DBG   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install && \
    \${CXX} \${CXXFLAGS} fuzz/tokener_parse_ex_fuzzer.cc \${AFL_PATH}/libfuzzStandalone.a -o \${INSTALL_PREFIX}/bin/json-c -I. -L\${INSTALL_PREFIX}/lib -ljson-c"

ENV BUILD_CMD_DATAFLOW_TAGS \
    "./configure --disable-shared --enable-static &&                             \
    make clean && make -j1"

ENV BUILD_CMD_DATAFLOW_ACCESS   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install && \
    \${CXX} \${CXXFLAGS} fuzz/tokener_parse_ex_fuzzer.cc \${AFL_PATH}/libfuzzStandalone.a -o \${INSTALL_PREFIX}/bin/json-c -I. -L\${INSTALL_PREFIX}/lib -ljson-c"
ENV BUILD_CMD_DATAFLOW_ACCESS_DBG   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install && \
    \${CXX} \${CXXFLAGS} fuzz/tokener_parse_ex_fuzzer.cc \${AFL_PATH}/libfuzzStandalone.a -o \${INSTALL_PREFIX}/bin/json-c -I. -L\${INSTALL_PREFIX}/lib -ljson-c"

ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} && \
    make clean && make -j && make install && \
    \${CXX} \${CXXFLAGS} fuzz/tokener_parse_ex_fuzzer.cc \${AFL_PATH}/libfuzzStandalone.a -o \${INSTALL_PREFIX}/bin/json-c -I. -L\${INSTALL_PREFIX}/lib -ljson-c"
ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS_DBG   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install && \
    \${CXX} \${CXXFLAGS} fuzz/tokener_parse_ex_fuzzer.cc \${AFL_PATH}/libfuzzStandalone.a -o \${INSTALL_PREFIX}/bin/json-c -I. -L\${INSTALL_PREFIX}/lib -ljson-c"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install && \
    \${CXX} \${CXXFLAGS} fuzz/tokener_parse_ex_fuzzer.cc \${AFL_PATH}/libfuzzStandalone.a -o \${INSTALL_PREFIX}/bin/json-c -I. -L\${INSTALL_PREFIX}/lib -ljson-c"
ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_DBG   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install && \
    \${CXX} \${CXXFLAGS} fuzz/tokener_parse_ex_fuzzer.cc \${AFL_PATH}/libfuzzStandalone.a -o \${INSTALL_PREFIX}/bin/json-c -I. -L\${INSTALL_PREFIX}/lib -ljson-c"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install && \
    \${CXX} \${CXXFLAGS} fuzz/tokener_parse_ex_fuzzer.cc \${AFL_PATH}/libfuzzStandalone.a -o \${INSTALL_PREFIX}/bin/json-c -I. -L\${INSTALL_PREFIX}/lib -ljson-c"
ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS_DBG   \
    "./configure --disable-shared --enable-static --prefix=\${INSTALL_PREFIX} &&     \
    make clean && make -j && make install && \
    \${CXX} \${CXXFLAGS} fuzz/tokener_parse_ex_fuzzer.cc \${AFL_PATH}/libfuzzStandalone.a -o \${INSTALL_PREFIX}/bin/json-c -I. -L\${INSTALL_PREFIX}/lib -ljson-c"

ADD whitelist.txt ${TARGET_SRC}/whitelist.txt
RUN ${WORKDIR}/build.sh

# Seeds
RUN find ${TARGET_SRC} -name '*.json' -type f -exec cp {} seeds/ \;
ADD seeds/* seeds/

CMD ["bash"]
