FROM dataflow/base
MAINTAINER adrian.herrera02@gmail.com

RUN curl -L ftp://ftp.pcre.org/pub/pcre/pcre2-10.34.tar.gz | tar xzvf -

ENV TARGET "pcre2"
ENV TARGET_SRC "pcre2-10.34"

ENV BUILD_CMD_AFL   \
    "./configure --disable-shared --enable-static --enable-fuzz-support --prefix=\${INSTALL_PREFIX} &&  \
    make clean && make -j && make install &&    \
    mv ${WORKDIR}/\${TARGET_SRC}/pcre2fuzzcheck \${INSTALL_PREFIX}/bin/"
ENV BUILD_CMD_AFL_DBG   \
    "./configure --disable-shared --enable-static --enable-fuzz-support --prefix=\${INSTALL_PREFIX} &&  \
    make clean && make -j && make install &&    \
    mv ${WORKDIR}/\${TARGET_SRC}/pcre2fuzzcheck \${INSTALL_PREFIX}/bin/"

ENV BUILD_CMD_ANGORA   \
    "./configure --disable-shared --enable-static --enable-fuzz-support --prefix=\${INSTALL_PREFIX} &&  \
    make clean && make -j && make install &&    \
    mv ${WORKDIR}/\${TARGET_SRC}/pcre2fuzzcheck \${INSTALL_PREFIX}/bin/"

ENV BUILD_CMD_DATAFLOW_TAGS \
    "./configure --disable-shared --enable-static --enable-fuzz-support &&                              \
    make clean && make -j1"

ENV BUILD_CMD_DATAFLOW_ACCESS   \
    "./configure --disable-shared --enable-static --enable-fuzz-support --prefix=\${INSTALL_PREFIX} &&  \
    make clean && make -j && make install &&    \
    mv ${WORKDIR}/\${TARGET_SRC}/pcre2fuzzcheck \${INSTALL_PREFIX}/bin/"
ENV BUILD_CMD_DATAFLOW_ACCESS_DBG   \
    "./configure --disable-shared --enable-static --enable-fuzz-support --prefix=\${INSTALL_PREFIX} &&  \
    make clean && make -j && make install &&    \
    mv ${WORKDIR}/\${TARGET_SRC}/pcre2fuzzcheck \${INSTALL_PREFIX}/bin/"

ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS   \
    "./configure --disable-shared --enable-static --enable-fuzz-support --prefix=\${INSTALL_PREFIX} &&  \
    make clean && make -j && make install &&    \
    mv ${WORKDIR}/\${TARGET_SRC}/pcre2fuzzcheck \${INSTALL_PREFIX}/bin/"
ENV BUILD_CMD_DATAFLOW_ACCESS_HEAPIFY_STRUCTS_DBG   \
    "./configure --disable-shared --enable-static --enable-fuzz-support --prefix=\${INSTALL_PREFIX} &&  \
    make clean && make -j && make install &&    \
    mv ${WORKDIR}/\${TARGET_SRC}/pcre2fuzzcheck \${INSTALL_PREFIX}/bin/"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX   \
    "./configure --disable-shared --enable-static --enable-fuzz-support --prefix=\${INSTALL_PREFIX} &&  \
    make clean && make -j && make install &&    \
    mv ${WORKDIR}/\${TARGET_SRC}/pcre2fuzzcheck \${INSTALL_PREFIX}/bin/"
ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_DBG   \
    "./configure --disable-shared --enable-static --enable-fuzz-support --prefix=\${INSTALL_PREFIX} &&  \
    make clean && make -j && make install &&    \
    mv ${WORKDIR}/\${TARGET_SRC}/pcre2fuzzcheck \${INSTALL_PREFIX}/bin/"

ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS   \
    "./configure --disable-shared --enable-static --enable-fuzz-support --prefix=\${INSTALL_PREFIX} &&  \
    make clean && make -j && make install &&    \
    mv ${WORKDIR}/\${TARGET_SRC}/pcre2fuzzcheck \${INSTALL_PREFIX}/bin/"
ENV BUILD_CMD_DATAFLOW_ACCESS_IDX_HEAPIFY_STRUCTS_DBG   \
    "./configure --disable-shared --enable-static --enable-fuzz-support --prefix=\${INSTALL_PREFIX} &&  \
    make clean && make -j && make install &&    \
    mv ${WORKDIR}/\${TARGET_SRC}/pcre2fuzzcheck \${INSTALL_PREFIX}/bin/"

ADD whitelist.txt ${WORKDIR}/${TARGET_SRC}
RUN ${WORKDIR}/build.sh

# Seeds
RUN cp ${TARGET_SRC}/testdata/* seeds/

CMD ["bash"]
