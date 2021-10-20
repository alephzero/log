import a0
import datetime
import enum
import json
import glob
import os
import pytest
import subprocess
import sys
import tempfile
import time
import threading
import types

# TODO(lshamis): Things to test:
# * unclean shutdown.


class RunLogger:
    class State(enum.Enum):
        DEAD = 0
        CREATED = 1
        STARTED = 2

    def __init__(self):
        self.logger_proc = None
        self.savepath = tempfile.TemporaryDirectory(prefix="/dev/shm/")

    def start(self, cfg):
        assert not self.logger_proc

        os.environ["A0_TOPIC"] = "test"
        a0.Cfg(a0.env.topic()).write(json.dumps(cfg))

        self.logger_proc = subprocess.Popen(
            ["valgrind", "--leak-check=full", "--error-exitcode=125", "/logger.bin"],
            env=os.environ.copy(),
        )
        time.sleep(3)

    def shutdown(self):
        if self.logger_proc:
            self.logger_proc.terminate()
            assert self.logger_proc.wait(3) == 0
            self.logger_proc = None

    def logged_packets(self):
        now = datetime.datetime.utcnow()
        # Want something like:
        # /dev/shm/_bpm26nc/2021/10/19/alephzero/foo.pubsub.a0@2021-10-19T21:43:52.866409862-00:00.a0
        pkts = {}
        for path in glob.glob(
            os.path.join(self.savepath.name, now.strftime("%Y/%m/%d"), "**/*@*.a0"),
            recursive=True,
        ):

            key = path.split("/")[-1].split(".")[0]

            pkts[key] = []
            reader = a0.ReaderSync(a0.File(path), a0.INIT_OLDEST, a0.ITER_NEXT)
            while reader.has_next():
                pkts[key].append(reader.next().payload.decode())

        return pkts


@pytest.fixture()
def sandbox():
    with tempfile.TemporaryDirectory(prefix="/dev/shm/") as tmp_root:
        os.environ["A0_ROOT"] = tmp_root
        logger = RunLogger()
        yield logger
        logger.shutdown()


def test_policy_save_all(sandbox):
    foo = a0.Publisher("foo")
    bar = a0.Publisher("bar")

    sandbox.start(
        {
            "savepath": sandbox.savepath.name,
            "rules": [
                {
                    "protocol": "pubsub",
                    "topic": "*",
                    "policies": [{"type": "save_all"}],
                },
            ],
        }
    )

    for i in range(10):
        foo.pub(f"foo_{i}")
        bar.pub(f"bar_{i}")

    time.sleep(0.1)

    sandbox.shutdown()

    assert sandbox.logged_packets() == {
        "foo": [f"foo_{i}" for i in range(10)],
        "bar": [f"bar_{i}" for i in range(10)],
    }


def test_policy_drop_all(sandbox):
    foo = a0.Publisher("foo")
    bar = a0.Publisher("bar")

    sandbox.start(
        {
            "savepath": sandbox.savepath.name,
            "rules": [
                {
                    "protocol": "pubsub",
                    "topic": "foo",
                    "policies": [{"type": "drop_all"}],
                },
                {
                    "protocol": "pubsub",
                    "topic": "*",
                    "policies": [{"type": "save_all"}],
                },
            ],
        }
    )

    for i in range(10):
        foo.pub(f"foo_{i}")
        bar.pub(f"bar_{i}")

    time.sleep(0.1)

    sandbox.shutdown()

    assert sandbox.logged_packets() == {"bar": [f"bar_{i}" for i in range(10)]}


def test_policy_count(sandbox):
    foo = a0.Publisher("foo")
    bar = a0.Publisher("bar")

    sandbox.start(
        {
            "savepath": sandbox.savepath.name,
            "rules": [
                {
                    "protocol": "pubsub",
                    "topic": "foo",
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
                                }
                            ],
                        }
                    ],
                }
            ],
        }
    )

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

    assert sandbox.logged_packets() == {
        "foo": ["foo_8", "foo_9", "foo_10", "foo_18", "foo_19"]
    }


def test_policy_time(sandbox):
    foo = a0.Publisher("foo")
    bar = a0.Publisher("bar")

    sandbox.start(
        {
            "savepath": sandbox.savepath.name,
            "rules": [
                {
                    "protocol": "pubsub",
                    "topic": "foo",
                    "policies": [
                        {
                            "type": "time",
                            "args": {
                                "save_prev": "2s",
                                "save_next": "500ms",
                            },
                            "triggers": [
                                {
                                    "type": "pubsub",
                                    "args": {
                                        "topic": "bar",
                                    },
                                }
                            ],
                        }
                    ],
                }
            ],
        }
    )

    for i in range(40):
        foo.pub(f"foo_{i}")
        if i == 20:
            bar.pub("save_0")
        time.sleep(0.25)

    time.sleep(0.1)

    sandbox.shutdown()

    assert sandbox.logged_packets() == {"foo": [f"foo_{i}" for i in range(13, 23)]}


def test_trigger_rate(sandbox):
    foo = a0.Publisher("foo")

    sandbox.start(
        {
            "savepath": sandbox.savepath.name,
            "rules": [
                {
                    "protocol": "pubsub",
                    "topic": "foo",
                    "policies": [
                        {
                            "type": "count",
                            "args": {
                                "save_next": 1,
                            },
                            "triggers": [
                                {
                                    "type": "rate",
                                    "args": {
                                        "hz": 2,
                                    },
                                }
                            ],
                        }
                    ],
                }
            ],
        }
    )

    for i in range(20):
        foo.pub(f"foo_{i}")
        time.sleep(0.25)

    time.sleep(0.1)

    sandbox.shutdown()

    assert len(sandbox.logged_packets()["foo"]) in [10, 11]


def test_trigger_cron(sandbox):
    foo = a0.Publisher("foo")

    sandbox.start(
        {
            "savepath": sandbox.savepath.name,
            "rules": [
                {
                    "protocol": "pubsub",
                    "topic": "foo",
                    "policies": [
                        {
                            "type": "count",
                            "args": {
                                "save_next": 1,
                            },
                            "triggers": [
                                {
                                    "type": "cron",
                                    "args": {
                                        "pattern": "*/2 * * * * *",
                                    },
                                }
                            ],
                        }
                    ],
                }
            ],
        }
    )

    for i in range(20):
        foo.pub(f"foo_{i}")
        time.sleep(0.25)

    time.sleep(0.1)

    sandbox.shutdown()

    assert len(sandbox.logged_packets()["foo"]) in [3, 4]
