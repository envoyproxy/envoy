Fault descriptions and backtraces are now written directly to stderr from the fatal signal handler,
and are no longer emitted to configured log sinks such as :option:`--log-path`. Deployments that
collect crash backtraces from the log file need to capture stderr instead.
