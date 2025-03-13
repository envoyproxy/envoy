### Overview

Envoy uses the [spdlog library](https://github.com/gabime/spdlog#readme) for logging
through a variety of Envoy specific macros.

### Concepts

#### Level

Log messages are emitted with a log level chosen from one of the following:
* trace
* debug
* info
* warn
* error
* critical

This log level can be used to restrict which log messages are actually
shown via the `setLevel()` method of `Envoy::Logger::Logger` or via the command
line argument `--l <level>`. Any messages which has a level less than the specified
level will be squelched.

In addition, the log level is typically show in the emitted log line. For example
in the following line, you can see the level is `debug`:

```
[2021-09-22 18:39:01.268][28][debug][pool] [source/common/conn_pool/conn_pool_base.cc:293] [C18299946955195659044] attaching to next stream
```

#### ID

In addition the the level, every log is emitted with an ID. This ID is not
a numeric ID (like a stream ID or a connection ID) but is instead a token that
is used to groups log messages in by category. The list of known ID is defined
in `ALL_LOGGER_IDS` from `source/common/common/logger.h`. Similar to level, these
IDs show up in log lines. For example in the following line, you can see the
ID is `pool`:

```
[2021-09-22 18:39:01.268][28][debug][pool] [source/common/conn_pool/conn_pool_base.cc:293] [C18299946955195659044] attaching to next stream
```

### APIs

#### ENVOY_LOG

Most log messages in Envoy are generated via the `ENVOY_LOG()` macro. For example:

```
ENVOY_LOG(debug, "subset lb: fallback load balancer disabled");
```

This macro takes the log level as the first argument and the log message as the
second argument. However the ID is not explicitly specified. Instead, the ID
typically comes via the class inheriting from `Logger::Loggable<ID>`. By doing this,
`ENVOY_LOG()` calls are able to find the relevant log ID.

#### ENVOY_LOG_TO_LOGGER

Under some circumstances, code will not be in a method of a class which extends
`Loggable`. In those cases, there are a couple of options. One is to use
`ENVOY_LOG_TO_LOGGER` and pass in an existing logger. This logger can come via
the caller, or by requesting the logger for a specific ID. For example:

```
          ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::pool), warn,
                              "Failed to create Http/3 client. Transport socket "
                              "factory is not configured correctly.");
```

#### ENVOY_LOG_MISC

As a last resort, the `ENVOY_LOG_MISC` macro can be used to log with the `misc` ID. For
example:

```
ENVOY_LOG_MISC(warn, "failed to enable core dump");
```
However, it is usually much better to log to a more specific ID.

#### ENVOY_CONN_LOG / ENVOY_STREAM_LOG

There is another API which can be used specifically for `Connection` or `Stream`
related log messages. `ENVOY_CONN_LOG` takes an additional `Connection` argument
and `ENVOY_STREAM_LOG` takes an additional `Stream` argument. These macros work
like `ENVOY_LOG` except that they prepend the log message with `[C123]` or
`[C123][S456` based on the connection/stream ID of the specified argument.
Note that the IDs here are the Envoy IDs *NOT* the on-the-wire IDs from HTTP/2
or HTTP/3.

#### ENVOY_TAGGED_LOG

The following logging API allows the call site to pass a key-value object (``std::map``) with additional
context information that would be printed in the final log line. Unless JSON application logging is enabled,
the output log line will prepend the log tags in the following format: '[Tags: "key1":"value1","key2":"value2"]'.
When JSON application logging is enabled, the output JSON log will also include the key-values as additional properties
in the JSON struct.
Example:

```
std::map<std::string, std::string> log_tags{{"key1","value1"},{"key2","value2"}};
ENVOY_TAGGED_LOG(debug, log_tags, "failed to perform the operation");
// output: [debug] [Tags: "key1":"value1","key2":"value2"] failed to perform the operation
```

#### ENVOY_TAGGED_CONN_LOG / ENVOY_TAGGED_STREAM_LOG

These logging APIs support all the properties as described for ENVOY_TAGGED_LOG, but allow the call site to pass
additional objects to add a connection ID or stream ID (with respect to the relevant macro).
When using these macros, an additional log tag will be added with the key "ConnectionId" or "StreamId" (or both).

Example:

```
std::map<std::string, std::string> log_tags{{"key1","value1"},{"key2","value2"}};
ENVOY_TAGGED_LOG(debug, log_tags, conn_, "failed to perform the operation");
ENVOY_TAGGED_LOG(debug, log_tags, stream_, "failed to perform the operation");
// output: [debug] [Tags: "ConnectionId":"10","StreamId":"11","key1":"value1","key2":"value2"] failed to perform the operation
```
