#!/bin/bash
cd "$(dirname "$0")"

set -e

docker build -t alephzero_log .

# TODO(lshamis): Take ipc container as arg.

docker run \
    --rm -it \
    --name=a0_log \
    --ipc=host \
    --pid=host \
    alephzero_log
