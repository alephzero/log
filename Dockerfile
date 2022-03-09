###########
# Builder #
###########

FROM ubuntu:20.04 as builder

RUN apt update && DEBIAN_FRONTEND="noninteractive" apt install -y \
    g++ make

WORKDIR /workdir
COPY . /workdir

RUN make clean && make bin/log -j

##########
# Deploy #
##########

FROM busybox:glibc

COPY --from=builder /workdir/bin/log /log.bin

ENTRYPOINT ["/log.bin"]
