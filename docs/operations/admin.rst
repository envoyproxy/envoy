.. _operations_admin_interface:

Administration interface
========================

Envoy exposes a :ref:`local administration interface <config_admin>` that can be used to query and
modify different aspects of the server.

.. http:get:: /

  Print a menu of all available options.

.. http:get:: /certs

  List out all loaded TLS certificates, including file name, serial number, and days until
  expiration.

.. http:get:: /clusters

  List out all configured :ref:`cluster manager <arch_overview_cluster_manager>` clusters. This
  information includes all discovered upstream hosts in each cluster along with per host statistics.
  This is useful for debugging service discovery issues. The per host statistics include:

  .. csv-table::
    :header: Name, Type, Description
    :widths: 1, 1, 2

    cx_total, Counter, Total connections
    cx_active, Gauge, Total active connections
    cx_connect_fail, Counter, Total connection failures
    rq_total, Counter, Total requests
    rq_timeout, Counter, Total timed out requests
    rq_active, Gauge, Total active requests
    healthy, String, The health status of the host. See below
    weight, Integer, Load balancing weight (1-100)
    zone, String, Service zone
    canary, Boolean, Whether the host is a canary
    success_rate, Double, "Request success rate (0-100). -1 if there was not enough
    :ref:`request volume<config_cluster_manager_cluster_outlier_detection_success_rate_request_volume>`
    to calculate it"

  Host health status
    A host is either healthy or unhealthy because of one or more different failing health states.
    If the host is healthy the ``healthy`` output will be equal to *healthy*.

    If the host is not healthy, the ``healthy`` output will be composed of one or more of the
    following strings:

    */failed_active_hc*: The host has failed an :ref:`active health check
    <config_cluster_manager_cluster_hc>`.

    */failed_outlier_check*: The host has failed an outlier detection check.

.. http:get:: /cpuprofiler

  Enable or disable the CPU profiler. Requires compiling with gperftools.

.. http:get:: /healthcheck/fail

  Fail inbound health checks. This requires the use of the HTTP :ref:`health check filter
  <config_http_filters_health_check>`. This is useful for draining a server prior to shutting it
  down or doing a full restart. Invoking this command will universally fail health check requests
  regardless of how the filter is configured (pass through, etc.).

.. http:get:: /healthcheck/ok

  Negate the effect of :http:get:`/healthcheck/fail`. This requires the use of the HTTP
  :ref:`health check filter <config_http_filters_health_check>`.

.. http:get:: /hot_restart_version

  See :option:`--hot-restart-version`.

.. http:get:: /logging

  Enable/disable different logging levels on different subcomponents. Generally only used during
  development.

.. http:get:: /quitquitquit

  Cleanly exit the server.

.. http:get:: /reset_counters

  Reset all counters to zero. This is useful along with :http:get:`/stats` during debugging. Note
  that this does not drop any data sent to statsd. It just effects local output of the
  :http:get:`/stats` command.

.. http:get:: /server_info

  Outputs information about the running server. Sample output looks like:

.. code-block:: none

  envoy 267724/RELEASE live 1571 1571 0

The fields are:

* Process name
* Compiled SHA and build type
* Health check state (live or draining)
* Current hot restart epoch uptime in seconds
* Total uptime in seconds (across all hot restarts)
* Current hot restart epoch

.. http:get:: /stats

  Outputs all statistics on demand. This includes only counters and gauges. Timers are not output as
  Envoy currently has no built in histogram support and relies on statsd for timer aggregation. This
  command is very useful for local debugging. See :ref:`here <operations_stats>` for more
  information.
