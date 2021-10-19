FROM ubuntu:21.04

RUN apt update && DEBIAN_FRONTEND="noninteractive" apt install -y \
    git python3-dev python3-pip valgrind
RUN python3 -m pip install -U \
    pip \
    wheel \
    setuptools \
    pytest \
    pytest-asyncio \
    requests \
    websockets==9.1

RUN python3 -m pip install alephzero

COPY --from=alephzero_logger /logger.bin /
COPY ./test /test
ENTRYPOINT ["python3", "-m", "pytest", "/test/test_logger.py"]
