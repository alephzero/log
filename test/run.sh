#!/bin/bash
set -e
cd "$(dirname "$0")"

docker build \
    -t alephzero_logger \
    --build-arg=mode=cov \
    -f ../Dockerfile \
    ..

docker build \
    -t alephzero_logger_test \
    -f ./Dockerfile \
    ..

docker run \
    --rm \
    -it \
    --pid=host \
    --ipc=host \
    alephzero_logger_test

    # --entrypoint=bash \
    # bash
