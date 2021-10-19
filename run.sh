#!/bin/bash
cd "$(dirname "$0")"

set -e

docker build -t alephzero_logger .

# TODO(lshamis): Take ipc container as arg.

# docker run \
#     --rm -it \
#     --name=a0_logger \
#     --ipc=host \
#     --pid=host \
#     alephzero_logger


docker run \
    --rm -it \
    --name=a0_logger \
    alephzero_logger
