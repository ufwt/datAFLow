FROM dataflow/binutils
MAINTAINER adrian.herrera02@gmail.com

ENV TARGET "readelf"
ENV TARGET_SRC "binutils-2.34"

# Nothing to build - already done in the binutils container. Just rename sot we
# can fuzz

RUN find . -maxdepth 1 -name 'binutils-*' -not -name ${TARGET_SRC} -type d  \
    -exec bash -c 'cp -rv ${0} ${0/binutils/readelf}' {} \;
RUN find . -maxdepth 1 -name 'binutils-*' -not -name ${TARGET_SRC} -type d \
    -exec rm -rf {} \+

# Seeds
ADD seeds/* seeds/

CMD ["bash"]
