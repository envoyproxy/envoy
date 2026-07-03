Added ``grpc_entries_flushed`` and ``grpc_entries_flush_failed`` counters to the
:ref:`gRPC access log statistics <config_access_log_stats>` to track whether log entries are
actually being delivered to the gRPC endpoint, complementing the existing
``logs_written``/``logs_dropped`` buffer-level counters.
