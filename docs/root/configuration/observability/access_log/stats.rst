.. _config_access_log_stats:

Statistics
==========

Currently only the gRPC and file based access logs have statistics.

gRPC access log statistics
--------------------------

The gRPC access log has statistics rooted at *access_logs.grpc_access_log.* with the following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   logs_written, Counter, Total log entries sent to the logger which were not dropped. This does not imply the logs have been flushed to the gRPC endpoint yet.
   logs_dropped, Counter, Total log entries dropped due to network or application level back up.
   grpc_entries_flushed, Counter, Total log entries in batches that were successfully submitted to the gRPC stream. Note that for the streaming gRPC ALS protocol there is no per-batch acknowledgement from the server; this counter reflects entries written to the gRPC send buffer.
   grpc_entries_flush_failed, Counter, Total log entries in batches where the gRPC send attempt failed (stream creation failure or stream above write buffer high-watermark). An entry counted here may later be counted in ``grpc_entries_flushed`` if the batch is retried successfully on the next flush interval.


File access log statistics
--------------------------

The file access log has statistics rooted at the *filesystem.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  write_buffered, Counter, Total number of times file data is moved to Envoy's internal flush buffer
  write_completed, Counter, Total number of times a file was successfully written
  write_failed, Counter, Total number of times an error occurred during a file write operation
  flushed_by_timer, Counter, Total number of times internal flush buffers are written to a file due to flush timeout
  reopen_failed, Counter, Total number of times a file was failed to be opened
  write_total_buffered, Gauge, Current total size of internal flush buffer in bytes

Fluentd access log statistics
-----------------------------

The Fluentd access log has statistics rooted at the *access_logs.fluentd.<stat_prefix>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  entries_lost, Counter, Total number of times an access log entry was discarded due to unavailable connection.
  entries_buffered, Counter, Total number of entries (access log record) that was buffered/
  events_sent, Counter, Total number of events (Fluentd Forward Mode events) sent to the upstream.
  reconnect_attempts, Counter, Total number of times an attempt to reconnect to the upstream has been made.
  connections_closed, Counter, Total number of times a connection to the upstream cluster was closed.
