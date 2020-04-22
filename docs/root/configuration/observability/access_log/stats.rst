.. _config_access_log_stats:

Statistics
==========

Currently only the gRPC access log has statistics.

gRPC access log statistics
--------------------------

The gRPC access log has statistics rooted at *access_logs.grpc_access_log.* with the following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   logs_written, Counter, Total log entries sent to the logger which were not dropped. This does not imply the logs have been flushed to the gRPC endpoint yet.
   logs_dropped, Counter, Total log entries dropped due to network or HTTP/2 back up.
