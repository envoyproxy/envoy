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
