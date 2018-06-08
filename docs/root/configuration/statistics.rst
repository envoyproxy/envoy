.. _statistics:

Statistics
==========

A few statistics are emitted to report statistics system behavior:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  stats.overflow, Counter, Total number of times Envoy cannot allocate a statistic due to a shortage of shared memory

Server
------

Server related statistics are rooted at *server.* with following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  uptime, Gauge, Current server uptime in seconds
  memory_allocated, Gauge, Current amount of allocated memory in bytes
  memory_heap_size, Gauge, Current reserved heap size in bytes
  live, Gauge, "1 if the server is not currently draining, 0 otherwise"
  parent_connections, Gauge, Total connections of the old Envoy process on hot restart
  total_connections, Gauge, Total connections of both new and old Envoy processes
  version, Gauge, Integer represented version number based on SCM revision
  days_until_first_cert_expiring, Gauge, Number of days until the next certificate being managed will expire
  hot_restart_epoch, Gauge, Current hot restart epoch

File system
-----------

Statistics related to file system are emitted in the *filesystem.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  write_buffered, Counter, Total number of times file data is moved to Envoy's internal flush buffer
  write_completed, Counter, Total number of times a file was written
  flushed_by_timer, Counter, Total number of times internal flush buffers are written to a file due to flush timeout
  reopen_failed, Counter, Total number of times a file was failed to be opened
  write_total_buffered, Gauge, Current total size of internal flush buffer in bytes
