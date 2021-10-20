###########
# Builder #
###########

FROM ubuntu:21.04 as builder

RUN apt update && DEBIAN_FRONTEND="noninteractive" apt install -y \
    g++ git make wget zlib1g-dev

RUN mkdir -p /alephzero && \
    cd /alephzero && \
    git clone --depth 1 https://github.com/alephzero/alephzero.git && \
    cd /alephzero/alephzero && \
    git submodule update --quiet --init --depth 1 && \
    make install -j A0_EXT_NLOHMANN=1

RUN mkdir -p /nlohmann && \
    cd /nlohmann && \
    wget https://github.com/nlohmann/json/releases/download/v3.9.1/json.hpp

RUN mkdir -p /mariusbancila && \
    cd /mariusbancila && \
    git clone https://github.com/mariusbancila/croncpp.git

WORKDIR /
COPY include /include
COPY logger.cpp /logger.cpp

# Move the following into a Makefile
ARG mode=opt
RUN g++ \
    -o /logger.bin \
    -std=c++20 \
    $([ "$mode" = "opt" ] && echo "-O3 -flto") \
    $([ "$mode" = "dbg" ] && echo "-O0 -g3") \
    $([ "$mode" = "cov" ] && echo "-O0 -g3 -fprofile-arcs -ftest-coverage --coverage") \
    -I/ \
    -I/include \
    -I/mariusbancila/croncpp/include \
    -DA0_EXT_NLOHMANN \
    /logger.cpp \
    -L/lib \
    -Wl,-Bstatic \
    -lalephzero \
    -Wl,-Bdynamic \
    -lpthread

##########
# Deploy #
##########

FROM ubuntu:21.04

COPY --from=builder /logger.bin /

ENTRYPOINT ["/logger.bin"]
