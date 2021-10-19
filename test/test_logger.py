import a0
import asyncio
import base64
import enum
import json
import os
import pytest
import random
import requests
import subprocess
import sys
import tempfile
import time
import threading
import types
import websockets

pytestmark = pytest.mark.asyncio

# TODO(lshamis): Things to test:
# * unclean shutdown.


class RunLogger:

    class State(enum.Enum):
        DEAD = 0
        CREATED = 1
        STARTED = 2

    def __init__(self):
        self.logger_proc = None

    def start(self, cfg):
        assert not self.logger_proc

        os.environ["A0_TOPIC"] = "test"
        a0.Cfg(a0.env.topic()).write(json.dumps(cfg))

        ns = types.SimpleNamespace()
        ns.state = RunLogger.State.CREATED
        ns.state_cv = threading.Condition()

        def check_ready(pkt):
            with ns.state_cv:
                ns.state = RunLogger.State.STARTED
                ns.state_cv.notify_all()

        sub = a0.Subscriber("logger_ready", a0.INIT_AWAIT_NEW, a0.ITER_NEXT,
                            check_ready)

        self.logger_proc = subprocess.Popen(
            [
                "valgrind", "--leak-check=full", "--error-exitcode=125",
                "/logger.bin"
            ],
            env=os.environ.copy(),
        )

        with ns.state_cv:
            assert ns.state_cv.wait_for(
                lambda: ns.state == RunLogger.State.STARTED, timeout=10)

    def shutdown(self):
        if self.logger_proc:
          self.logger_proc.terminate()
          assert self.logger_proc.wait(1.0) == 0
          self.logger_proc = None


@pytest.fixture()
def sandbox():
    with tempfile.TemporaryDirectory(prefix="/dev/shm/") as tmp_root:
      os.environ["A0_ROOT"] = tmp_root
      logger = RunLogger()
      yield logger
      logger.shutdown()

def test_basic(sandbox):
    savepath = tempfile.TemporaryDirectory(prefix="/dev/shm/")
    sandbox.start([
      {
        "searchpath": os.environ["A0_ROOT"],
        "savepath": savepath.name,
        "match": [
          {"protocol": "pubsub", "pattern": "**/*"},
          {"protocol": "log", "pattern": "**/*"},
          {"protocol": "cfg", "pattern": "**/*"},
        ],
        "policies": [
          {
            "type": "count",
            "args": {
              "save_prev": 2,
              "save_next": 1,
            },
            "triggers": [
              {
                "type": "pubsub",
                "args": {
                  "topic": "bar",
                },
              },
            ],
          }
        ]
      },
    ])

    foo = a0.Publisher("foo")
    bar = a0.Publisher("bar")

    for i in range(10):
      foo.pub(f"foo_{i}")
    time.sleep(0.1)

    bar.pub("save_0")
    time.sleep(0.1)

    for i in range(10, 20):
      foo.pub(f"foo_{i}")
    time.sleep(0.1)

    bar.pub("save_1")
    time.sleep(0.1)

    sandbox.shutdown()

    import glob
    srcfiles = glob.glob(f'{os.environ["A0_ROOT"]}/**/*', recursive=True)
    dstfiles = glob.glob(f'{savepath.name}/**/*', recursive=True)

    # os.system(f"ls -lR {savepath.name}")
    # assert os.listdir(os.environ["A0_ROOT"] + '/alephzero') == ["foo"]
    # assert os.listdir("/tmp/logs/") == ["foo"]
    assert dstfiles == ["foo"]
