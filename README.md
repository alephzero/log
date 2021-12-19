# AlephZero Logger

This logger is a stand-alone application that listens in on AlephZero communications and saves the packets to long term storage.

The files are saved in a specified directory with the following subdirectory structure:

    savepath/YYYY/MM/DD/topic@timestamp.a0

These files are AlephZero files, and can therefore be read with `a0::Reader`s.

## What To Save

### Rules

The logger is configured with an ordered list of `rules`.

**NOTE**: The rules are "first-match-wins". If multiple rules would apply to a topic, the first one is selected. Selection is based on `protocol` and `topic`.

### Protocol and Topic

Each rule is required to specify a `protocol` and `topic`, which help find the AlephZero channels of communication.

`protocol` must be one of:
* `pubsub`: Messages produced by `a0::Publisher`.
* `rpc`: Requests sent by `a0::RpcClient` and responses from `a0::RpcServer`.
* `prpc`: Requests sent by `a0::PrpcClient` and responses from `a0::PrpcServer`.
* `log`: Messages witten by `a0::Logger`.
* `cfg`: Configuration managed by `a0::Cfg`.
* `file`: Bypasses protocol specific templates.

`topic` may be exact or may contain a glob `*` or `**`. For example:
* `foo`: will match to topic `foo`, but not `foo2` and not `foo/bar` and not `bar/foo`
* `foo*`: will match to topic `foo` and `foo2` and not `foo/bar` and not `bar/foo`
* `*`: will match to topic `foo` and `foo2` and not `foo/bar` and not `bar/foo`
* `*/*`: will match to topic `foo/bar`, but not `foo` and not `foo2` and not `a/b/c`
* `**/*`: will match to everything

### Policies

Once a topic has been discovered, the matching `rule` defines a set of `policies` which indicate what and when to save.

A rule can have multiple `policies`, each specifying:
* `type`: the plugin that will interpret, buffer, and handle messages.
* `args`: arguments passed to the policy plugin.
* `triggers`: plugins that will edge trigger the policy plugin.

Available `policies`:
* `save_all`: saves all messages.
* `drop_all`: drops all messages.
* `time`: save messages within a time window around a triggering event. Required `args` are `save_prev` and `save_next`. For example `{ "save_prev": "2s", "save_next": "500ms" }`.
* `count`: save a fixed number of messages around a triggering event. Required `args` are `save_prev` and `save_next`. For example `{ "save_prev": 5, "save_next": 3 }`.

### Triggers

A `policy` can have multiple `triggers`, each configured with a `type` and `args`.

Available `triggers`:
* `pubsub`: fires when a message is published on a given `topic`.
* `rate`: fires at a regular frequency `hz` or `period`.
* `cron`: fires at regular intervals as defined by the cron `pattern`.


## Extra Controls

### Logfile Size

A logfile, by default, is no more than 128MB and no more than 1 hour. Once a limit is reached, the logfile is closed and a new one is created.

To change the logfile duration, a global config `default_max_logfile_duration` can be set. Rules can be set their own `max_logfile_duration`.

Same for `default_max_logfile_size` and `max_logfile_size`.

### Record Start Time

The logger is often started in parallel with other processes, and the launch time, relative to the other processes is variable. By default, the logger will record starting with packets published up to 30s prior to the start of the logger.

`start_time_mono` can be set to a custom mono timestamp to provide an alternate start time.

<details>
<summary><b>Over-Complicated Example</b></summary>

```js
{
  // Where to save the logs.
  "savepath": "/nfs/logs",
  "rules": [
    // Save all application logs.
    {
      "protocol": "log",
      "topic": "**/*",
      "policies": [{"type": "save_all"}],
    },
    // Save all configuration changes.
    {
      "protocol": "cfg",
      "topic": "**/*",
      "policies": [{"type": "save_all"}],
    },
    // Save all pubsub messages to the `logkeep/*` topic.
    {
      "protocol": "pubsub",
      "topic": "logkeep/*",
      "policies": [{"type": "save_all"}],
    },
    // Save 2m of `camera_*` messages every hour and 10m around `critical_failure`.
    {
      "protocol": "pubsub",
      "topic": "camera_*",
      "policies": [{
        "type": "time",
        "args": {
          "save_prev": "1m",
          "save_next": "1m",
        },
        "triggers": [{
          "type": "cron",
          "args": {
            "pattern": "0 0 * ? * *"  // every hour
          }
        }],
      }, {
        "type": "time",
        "args": {
          "save_prev": "5m",
          "save_next": "5m",
        },
        "triggers": [{
          "type": "pubsub",
          "args": {
            "topic": "critical_failure"
          }
        }],
      }],
    },
    // Save imu messages at 2hz.
    {
      "protocol": "pubsub",
      "topic": "imu_*",
      "policies": [{
        "type": "count",
        "args": {
          "save_prev": 2,
          "save_next": 2,
        },
        "triggers": [{
          "type": "rate",
          "args": {
            "hz": 2,
          }
        }],
      }],
    },
    // Save all other pubsub.
    {
      "protocol": "pubsub",
      "topic": "**/*",
      "policies": [{"type": "save_all"}],
    },
  ],
}
```
</details>
