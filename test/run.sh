#!/bin/bash
set -e
cd "$(dirname "$0")"

docker build \
    -t alephzero/log:cov \
    --build-arg=mode=cov \
    -f ../Dockerfile \
    ..

docker build \
    -t alephzero/log_test \
    -f ./Dockerfile \
    ..

docker run \
    --rm \
    -it \
    --pid=host \
    --ipc=host \
    alephzero/log_test
