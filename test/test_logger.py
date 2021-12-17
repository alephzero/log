import a0
import datetime
import enum
import glob
import json
import os
import pytest
import re
import subprocess
import tempfile
import time

# TODO(lshamis): Things to test:
# * unclean shutdown.


# From https://kalnytskyi.com/howto/assert-str-matches-regex-in-pytest/
class pytest_regex:
    def __init__(self, pattern, flags=0):
        self._regex = re.compile(pattern, flags)

    def __eq__(self, actual):
        return bool(self._regex.match(actual))

    def __repr__(self):
        return self._regex.pattern


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
            [
                "valgrind", "--leak-check=full", "--error-exitcode=125",
                "/logger.bin"
            ],
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
                os.path.join(self.savepath.name, now.strftime("%Y/%m/%d"),
                             "**/*@*.a0"),
                recursive=True,
        ):

            key = path.split("/")[-1].split(".")[0]

            pkts[key] = []
            reader = a0.ReaderSync(a0.File(path), a0.INIT_OLDEST, a0.ITER_NEXT)
            while reader.can_read():
                pkts[key].append(reader.read().payload.decode())

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

    sandbox.start({
        "savepath":
        sandbox.savepath.name,
        "rules": [
            {
                "protocol": "pubsub",
                "topic": "*",
                "policies": [{
                    "type": "save_all"
                }],
            },
        ],
    })

    for i in range(10):
        foo.pub(f"foo_{i}")
        bar.pub(f"bar_{i}")

    time.sleep(0.5)

    sandbox.shutdown()

    assert sandbox.logged_packets() == {
        "foo": [f"foo_{i}" for i in range(10)],
        "bar": [f"bar_{i}" for i in range(10)],
    }


def test_policy_drop_all(sandbox):
    foo = a0.Publisher("foo")
    bar = a0.Publisher("bar")

    sandbox.start({
        "savepath":
        sandbox.savepath.name,
        "rules": [
            {
                "protocol": "pubsub",
                "topic": "foo",
                "policies": [{
                    "type": "drop_all"
                }],
            },
            {
                "protocol": "pubsub",
                "topic": "*",
                "policies": [{
                    "type": "save_all"
                }],
            },
        ],
    })

    for i in range(10):
        foo.pub(f"foo_{i}")
        bar.pub(f"bar_{i}")

    time.sleep(0.5)

    sandbox.shutdown()

    assert sandbox.logged_packets() == {"bar": [f"bar_{i}" for i in range(10)]}


def test_policy_count(sandbox):
    foo = a0.Publisher("foo")
    bar = a0.Publisher("bar")

    sandbox.start({
        "savepath":
        sandbox.savepath.name,
        "rules": [{
            "protocol":
            "pubsub",
            "topic":
            "foo",
            "policies": [{
                "type":
                "count",
                "args": {
                    "save_prev": 2,
                    "save_next": 1,
                },
                "triggers": [{
                    "type": "pubsub",
                    "args": {
                        "topic": "bar",
                    },
                }],
            }],
        }],
    })

    for i in range(10):
        foo.pub(f"foo_{i}")
    time.sleep(0.1)

    bar.pub("save_0")
    time.sleep(0.1)

    for i in range(10, 20):
        foo.pub(f"foo_{i}")
    time.sleep(0.1)

    bar.pub("save_1")
    time.sleep(0.5)

    sandbox.shutdown()

    assert sandbox.logged_packets() == {
        "foo": ["foo_8", "foo_9", "foo_10", "foo_18", "foo_19"]
    }


def test_policy_time(sandbox):
    foo = a0.Publisher("foo")
    bar = a0.Publisher("bar")

    sandbox.start({
        "savepath":
        sandbox.savepath.name,
        "rules": [{
            "protocol":
            "pubsub",
            "topic":
            "foo",
            "policies": [{
                "type":
                "time",
                "args": {
                    "save_prev": "2s",
                    "save_next": "500ms",
                },
                "triggers": [{
                    "type": "pubsub",
                    "args": {
                        "topic": "bar",
                    },
                }],
            }],
        }],
    })

    for i in range(40):
        foo.pub(f"foo_{i}")
        if i == 20:
            bar.pub("save_0")
        time.sleep(0.25)

    time.sleep(0.5)

    sandbox.shutdown()

    assert sandbox.logged_packets() == {
        "foo": [f"foo_{i}" for i in range(13, 23)]
    }


def test_trigger_rate(sandbox):
    foo = a0.Publisher("foo")

    sandbox.start({
        "savepath":
        sandbox.savepath.name,
        "rules": [{
            "protocol":
            "pubsub",
            "topic":
            "foo",
            "policies": [{
                "type": "count",
                "args": {
                    "save_next": 1,
                },
                "triggers": [{
                    "type": "rate",
                    "args": {
                        "hz": 2,
                    },
                }],
            }],
        }],
    })

    for i in range(20):
        foo.pub(f"foo_{i}")
        time.sleep(0.25)

    time.sleep(0.5)

    sandbox.shutdown()

    assert len(sandbox.logged_packets()["foo"]) in [10, 11]


def test_trigger_cron(sandbox):
    foo = a0.Publisher("foo")

    sandbox.start({
        "savepath":
        sandbox.savepath.name,
        "rules": [{
            "protocol":
            "pubsub",
            "topic":
            "foo",
            "policies": [{
                "type":
                "count",
                "args": {
                    "save_next": 1,
                },
                "triggers": [{
                    "type": "cron",
                    "args": {
                        "pattern": "*/2 * * * * *",
                    },
                }],
            }],
        }],
    })

    for i in range(20):
        foo.pub(f"foo_{i}")
        time.sleep(0.25)

    time.sleep(0.5)

    sandbox.shutdown()

    assert len(sandbox.logged_packets()["foo"]) in [3, 4]


def test_max_logfile_size(sandbox):
    foo = a0.Publisher("foo")
    bar = a0.Publisher("bar")

    sandbox.start({
        "savepath":
        sandbox.savepath.name,
        "default_max_logfile_size":
        "2MiB",
        "rules": [
            {
                "protocol": "pubsub",
                "topic": "foo",
                "policies": [{
                    "type": "save_all"
                }],
            },
            {
                "protocol": "pubsub",
                "topic": "bar",
                "max_logfile_size": "4MiB",
                "policies": [{
                    "type": "save_all"
                }],
            },
        ],
    })

    announcements = []

    def on_announce(pkt):
        announcements.append(json.loads(pkt.payload.decode()))

    s = a0.Subscriber(  # noqa:F841
        "test/announce", a0.INIT_OLDEST, a0.ITER_NEXT, on_announce)

    msg = "a" * (1024 * 1024 // 2 - 1024)  # Half a megabyte minus epsilon.
    for _ in range(16):
        foo.pub(msg)
        bar.pub(msg)

    time.sleep(1)

    sandbox.shutdown()

    topic_counter = {"foo.pubsub.a0": 0, "bar.pubsub.a0": 0}
    for announcement in announcements:
        assert announcement["action"] in ["opened", "closed"]
        assert announcement["relative_path"] in [
            "foo.pubsub.a0", "bar.pubsub.a0"
        ]
        assert (announcement["read_file"] ==
                f"{os.environ['A0_ROOT']}/{announcement['relative_path']}")
        assert announcement["write_file"] == pytest_regex(
            f"{sandbox.savepath.name}/\\d{{4}}/\\d{{2}}/\\d{{2}}/{announcement['relative_path']}@\\d{{4}}-\\d{{2}}-\\d{{2}}T\\d{{2}}:\\d{{2}}:\\d{{2}}.\\d{{9}}-\\d{{2}}:\\d{{2}}.a0"  # noqa: E501
        )
        topic_counter[announcement["relative_path"]] += 1

    assert topic_counter == {"foo.pubsub.a0": 8, "bar.pubsub.a0": 4}


def test_max_logfile_duration(sandbox):
    foo = a0.Publisher("foo")
    bar = a0.Publisher("bar")

    sandbox.start({
        "savepath":
        sandbox.savepath.name,
        "default_max_logfile_duration":
        "2s",
        "rules": [
            {
                "protocol": "pubsub",
                "topic": "foo",
                "policies": [{
                    "type": "save_all"
                }],
            },
            {
                "protocol": "pubsub",
                "topic": "bar",
                "max_logfile_duration": "4s",
                "policies": [{
                    "type": "save_all"
                }],
            },
        ],
    })

    announcements = []

    def on_announce(pkt):
        announcements.append(json.loads(pkt.payload.decode()))

    s = a0.Subscriber(  # noqa: F841
        "test/announce", a0.INIT_OLDEST, a0.ITER_NEXT, on_announce)

    msg = "msg"
    for _ in range(10):
        foo.pub(msg)
        bar.pub(msg)
        time.sleep(0.5)

    sandbox.shutdown()

    topic_counter = {"foo.pubsub.a0": 0, "bar.pubsub.a0": 0}
    for announcement in announcements:
        assert announcement["action"] in ["opened", "closed"]
        assert announcement["relative_path"] in [
            "foo.pubsub.a0", "bar.pubsub.a0"
        ]
        assert (announcement["read_file"] ==
                f"{os.environ['A0_ROOT']}/{announcement['relative_path']}")
        assert announcement["write_file"] == pytest_regex(
            f"{sandbox.savepath.name}/\\d{{4}}/\\d{{2}}/\\d{{2}}/{announcement['relative_path']}@\\d{{4}}-\\d{{2}}-\\d{{2}}T\\d{{2}}:\\d{{2}}:\\d{{2}}.\\d{{9}}-\\d{{2}}:\\d{{2}}.a0"  # noqa: E501
        )
        topic_counter[announcement["relative_path"]] += 1

    assert topic_counter == {"foo.pubsub.a0": 6, "bar.pubsub.a0": 4}
