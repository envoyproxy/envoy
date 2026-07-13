Added :option:`--log-stacktrace-single-entry` CLI option to emit the entire stack trace
in a single ``ENVOY_LOG`` call instead of one call per frame. Each frame is still delimited
by a newline within the message. This is useful for log aggregation systems where each
log call produces a separate log entry (e.g. JSON logging).
