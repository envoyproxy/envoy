.. _operations_cli:

Command line options
====================

Envoy is driven both by a JSON configuration file as well as a set of command line options. The
following are the command line options that Envoy supports.

.. option:: -c <path string>, --config-path <path string>

  *(required)* The path to the :ref:`JSON configuration file <config>`.

.. option:: -a <path string>, --admin-address-path <path string>

  *(optional)* The output file path where the admin address and port will be written.

.. option:: --base-id <integer>

  *(optional)* The base ID to use when allocating shared memory regions. Envoy uses shared memory
  regions during :ref:`hot restart <arch_overview_hot_restart>`. Most users will never have to
  set this option. However, if Envoy needs to be run multiple times on the same machine, each
  running Envoy will need a unique base ID so that the shared memory regions do not conflict.

.. option:: --concurrency <integer>

  *(optional)* The number of :ref:`worker threads <arch_overview_threading>` to run. If not
  specified defaults to the number of hardware threads on the machine.

.. option:: -l <string>, --log-level <string>

  *(optional)* The logging level. Non developers should generally never set this option. See the
  help text for the available log levels and the default.

.. option:: --restart-epoch <integer>

  *(optional)* The :ref:`hot restart <arch_overview_hot_restart>` epoch. (The number of times
  Envoy has been hot restarted instead of a fresh start). Defaults to 0 for the first start. This
  option tells Envoy whether to attempt to create the shared memory region needed for hot restart,
  or whether to open an existing one. It should be incremented every time a hot restart takes place.
  The :ref:`hot restart wrapper <operations_hot_restarter>` sets the *RESTART_EPOCH* environment
  variable which should be passed to this option in most cases.

.. option:: --hot-restart-version

  *(optional)* Outputs an opaque hot restart compatibility version for the binary. This can be
  matched against the output of the :http:get:`/hot_restart_version` admin endpoint to determine
  whether the new binary and the running binary are hot restart compatible.

.. option:: --service-cluster <string>

  *(optional)* Defines the local service cluster name where Envoy is running. Though optional,
  it should be set if any of the following features are used: :ref:`statsd
  <arch_overview_statistics>`, :ref:`health check cluster verification
  <config_cluster_manager_cluster_hc_service_name>`, :ref:`runtime override directory
  <config_runtime_override_subdirectory>`, :ref:`user agent addition
  <config_http_conn_man_add_user_agent>`, :ref:`HTTP global rate limiting
  <config_http_filters_rate_limit>`, :ref:`CDS <config_cluster_manager_cds>`, and
  :ref:`HTTP tracing <arch_overview_tracing>`.

.. option:: --service-node <string>

  *(optional)* Defines the local service node name where Envoy is running. Though optional,
  it should be set if any of the following features are used: :ref:`statsd
  <arch_overview_statistics>`, :ref:`CDS <config_cluster_manager_cds>`, and
  :ref:`HTTP tracing <arch_overview_tracing>`.

.. option:: --service-zone <string>

  *(optional)* Defines the local service zone where Envoy is running. Though optional, it should
  be set if discovery service routing is used and the discovery service exposes :ref:`zone data
  <config_cluster_manager_sds_api_host_az>`.

.. option:: --file-flush-interval-msec <integer>

  *(optional)* The file flushing interval in milliseconds. Defaults to 10 seconds.
  This setting is used during file creation to determine the duration between flushes
  of buffers to files. The buffer will flush every time it gets full, or every time
  the interval has elapsed, whichever comes first. Adjusting this setting is useful
  when tailing :ref:`access logs <arch_overview_http_access_logs>` in order to
  get more (or less) immediate flushing.

.. option:: --drain-time-s <integer>

  *(optional)* The time in seconds that Envoy will drain connections during a hot restart. See the
  :ref:`hot restart overview <arch_overview_hot_restart>` for more information. Defaults to 600
  seconds (10 minutes). Generally the drain time should be less than the parent shutdown time
  set via the :option:`--parent-shutdown-time-s` option. How the two settings are configured
  depends on the specific deployment. In edge scenarios, it might be desirable to have a very long
  drain time. In service to service scenarios, it might be possible to make the drain and shutdown
  time much shorter (e.g., 60s/90s).

.. option:: --parent-shutdown-time-s <integer>

  *(optional)* The time in seconds that Envoy will wait before shutting down the parent process
  during a hot restart. See the :ref:`hot restart overview <arch_overview_hot_restart>` for more
  information. Defaults to 900 seconds (15 minutes).
