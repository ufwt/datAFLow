FROM dataflow/binutils
MAINTAINER adrian.herrera02@gmail.com

ENV TARGET "cxxfilt"
ENV TARGET_SRC "binutils-2.34"

# Nothing to build - already done in binutils. Just rename so we can fuzz

RUN find . -maxdepth 1 -name 'binutils-*' -not -name ${TARGET_SRC} -type d  \
    -exec bash -c 'cp -rv ${0} ${0/binutils/cxxfilt}' {} \;
RUN find . -maxdepth 1 -name 'binutils-*' -not -name ${TARGET_SRC} -type d \
    -exec rm -rf {} \+

# Seeds
RUN echo "" > seeds/empty

CMD ["bash"]
