.. _statistics:

Statistics
==========

.. _server_statistics:

Server
------

Server related statistics are rooted at *server.* with following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  uptime, Gauge, Current server uptime in seconds
  concurrency, Gauge, Number of worker threads
  memory_allocated, Gauge, Current amount of allocated memory in bytes. Total of both new and old Envoy processes on hot restart.
  memory_heap_size, Gauge, Current reserved heap size in bytes. New Envoy process heap size on hot restart.
  memory_physical_size, Gauge, Current estimate of total bytes of the physical memory. New Envoy process physical memory size on hot restart.
  live, Gauge, "1 if the server is not currently draining, 0 otherwise"
  state, Gauge, Current :ref:`State <envoy_v3_api_field_admin.v3.ServerInfo.state>` of the Server.
  parent_connections, Gauge, Total connections of the old Envoy process on hot restart
  total_connections, Gauge, Total connections of both new and old Envoy processes
  version, Gauge, Integer represented version number based on SCM revision or :ref:`stats_server_version_override <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.stats_server_version_override>` if set.
  days_until_first_cert_expiring, Gauge, Number of days until the next certificate being managed will expire
  seconds_until_first_ocsp_response_expiring, Gauge, Number of seconds until the next OCSP response being managed will expire
  hot_restart_epoch, Gauge, Current hot restart epoch -- an integer passed via command line flag `--restart-epoch` usually indicating generation.
  hot_restart_generation, Gauge, Current hot restart generation -- like hot_restart_epoch but computed automatically by incrementing from parent.
  initialization_time_ms, Histogram, Total time taken for Envoy initialization in milliseconds. This is the time from server start-up until the worker threads are ready to accept new connections
  debug_assertion_failures, Counter, Number of debug assertion failures detected in a release build if compiled with `--define log_debug_assert_in_release=enabled` or zero otherwise
  envoy_bug_failures, Counter, Number of envoy bug failures detected in a release build. File or report the issue if this increments as this may be serious.
  static_unknown_fields, Counter, Number of messages in static configuration with unknown fields
  dynamic_unknown_fields, Counter, Number of messages in dynamic configuration with unknown fields

.. _server_compilation_settings_statistics:

Server Compilation Settings
---------------------------

Server Compilation Settings related statistics are rooted at *server.compilation_settings.* with following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  fips_mode, Gauge, Integer representing whether the envoy build is FIPS compliant or not