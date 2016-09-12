Command line options
====================

Envoy is driven both by a JSON configuration file as well as a set of command line options. The
following are the command line options that Envoy supports.

.. option:: -c <path string>, --config-path <path string>

  *(required)* The path to the :ref:`JSON configuration file <config>`.

.. option:: --base-id <integer>

  *(optional)* The base ID to use when allocating shared memory regions. Envoy uses shared memory
  regions during :ref:`hot restart <arch_overview_hot_restart>`. Most users will never have to
  set this option. However, if Envoy needs to be run multiple times on the same machine, each
  running Envoy will need a unique base ID so that the shared memory regions do not conflict.

.. option:: --concurrency <integer>

  *(optional)* The number of :ref:`worker threads <arch_overview_threading>` to run. If not
  specified defaults to the number of hardware threads on the machine.

.. option:: -l <integer>, --log-level <integer>

  *(optional)* The logging level. Defaults to *NOTICE*. Non developers should never set this option.

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
  <config_http_filters_rate_limit>`, and :ref:`HTTP tracing <arch_overview_tracing>`.

.. option:: --service-node <string>

  *(optional)* Defines the local service node name where Envoy is running. Though optional,
  it should be set if any of the following features are used: :ref:`statsd
  <arch_overview_statistics>`, and :ref:`HTTP tracing <arch_overview_tracing>`.

.. option:: --service-zone <string>

  *(optional)* Defines the local service zone where Envoy is running. Though optional, it should
  be set if discovery service routing is used and the discovery service exposes :ref:`zone data
  <config_cluster_manager_sds_api_host_az>`.

.. option:: --flush-interval-msec <string>

  *(optional)* The time duration in msec between flushing of logs. Bare in mind 
  that the logs have a buffer that when full flushes to the file specified in the envoy
  config. However, if the interval is up and the buffer hasn't filled up, the log will flush. 
  This option currently, universally changes the rate of log flushing for all logs created with 
  the api call `createFile`. An example of where this affects log flushing can be seen here 
  :repo:`/source/common/mongo/proxy.cc`.
