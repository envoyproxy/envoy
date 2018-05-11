.. _operations_admin_interface:

Administration interface
========================

Envoy exposes a local administration interface that can be used to query and
modify different aspects of the server:

* :ref:`v1 API reference <config_admin_v1>`
* :ref:`v2 API reference <envoy_api_msg_config.bootstrap.v2.Admin>`

.. _operations_admin_interface_security:

.. attention::

  The administration interface in its current form both allows destructive operations to be
  performed (e.g., shutting down the server) as well as potentially exposes private information
  (e.g., stats, cluster names, cert info, etc.). It is **critical** that access to the
  administration interface is only allowed via a secure network. It is also **critical** that hosts
  that access the administration interface are **only** attached to the secure network (i.e., to
  avoid CSRF attacks). This involves setting up an appropriate firewall or optimally only allowing
  access to the administration listener via localhost. This can be accomplished with a v2
  configuration like the following:

  .. code-block:: yaml

    admin:
      access_log_path: /tmp/admin_access.log
      address:
        socket_address: { address: 127.0.0.1, port_value: 9901 }

  In the future additional security options will be added to the administration interface. This
  work is tracked in `this <https://github.com/envoyproxy/envoy/issues/2763>`_ issue.

  All mutations should be sent as HTTP POST operations. For a limited time, they will continue
  to work with HTTP GET, with a warning logged.

.. http:get:: /

  Render an HTML home page with a table of links to all available options.

.. http:get:: /help

  Print a textual table of all available options.

.. http:get:: /certs

  List out all loaded TLS certificates, including file name, serial number, and days until
  expiration.

.. _operations_admin_interface_clusters:

.. http:get:: /clusters

  List out all configured :ref:`cluster manager <arch_overview_cluster_manager>` clusters. This
  information includes all discovered upstream hosts in each cluster along with per host statistics.
  This is useful for debugging service discovery issues.

  Cluster manager information
    - ``version_info`` string -- the version info string of the last loaded
      :ref:`CDS<config_cluster_manager_cds>` update.
      If envoy does not have :ref:`CDS<config_cluster_manager_cds>` setup, the
      output will read ``version_info::static``.

  Cluster wide information
    - :ref:`circuit breakers<config_cluster_manager_cluster_circuit_breakers>` settings for all priority settings.

    - Information about :ref:`outlier detection<arch_overview_outlier_detection>` if a detector is installed. Currently
      :ref:`success rate average<arch_overview_outlier_detection_ejection_event_logging_cluster_success_rate_average>`,
      and :ref:`ejection threshold<arch_overview_outlier_detection_ejection_event_logging_cluster_success_rate_ejection_threshold>`
      are presented. Both of these values could be ``-1`` if there was not enough data to calculate them in the last
      :ref:`interval<config_cluster_manager_cluster_outlier_detection_interval_ms>`.

    - ``added_via_api`` flag -- ``false`` if the cluster was added via static configuration, ``true``
      if it was added via the :ref:`CDS<config_cluster_manager_cds>` api.

  Per host statistics
    .. csv-table::
      :header: Name, Type, Description
      :widths: 1, 1, 2

      cx_total, Counter, Total connections
      cx_active, Gauge, Total active connections
      cx_connect_fail, Counter, Total connection failures
      rq_total, Counter, Total requests
      rq_timeout, Counter, Total timed out requests
      rq_success, Counter, Total requests with non-5xx responses
      rq_error, Counter, Total requests with 5xx responses
      rq_active, Gauge, Total active requests
      healthy, String, The health status of the host. See below
      weight, Integer, Load balancing weight (1-100)
      zone, String, Service zone
      canary, Boolean, Whether the host is a canary
      success_rate, Double, "Request success rate (0-100). -1 if there was not enough
      :ref:`request volume<config_cluster_manager_cluster_outlier_detection_success_rate_request_volume>`
      in the :ref:`interval<config_cluster_manager_cluster_outlier_detection_interval_ms>`
      to calculate it"

  Host health status
    A host is either healthy or unhealthy because of one or more different failing health states.
    If the host is healthy the ``healthy`` output will be equal to *healthy*.

    If the host is not healthy, the ``healthy`` output will be composed of one or more of the
    following strings:

    */failed_active_hc*: The host has failed an :ref:`active health check
    <config_cluster_manager_cluster_hc>`.

    */failed_eds_health*: The host was marked unhealthy by EDS.

    */failed_outlier_check*: The host has failed an outlier detection check.

.. _operations_admin_interface_config_dump:

.. http:get:: /config_dump

  Dump currently loaded configuration from various Envoy components as JSON-serialized proto
  messages. See the :ref:`response definition <envoy_api_msg_admin.v2alpha.ConfigDump>` for more
  information.

.. http:post:: /cpuprofiler

  Enable or disable the CPU profiler. Requires compiling with gperftools.

.. _operations_admin_interface_healthcheck_fail:

.. http:post:: /healthcheck/fail

  Fail inbound health checks. This requires the use of the HTTP :ref:`health check filter
  <config_http_filters_health_check>`. This is useful for draining a server prior to shutting it
  down or doing a full restart. Invoking this command will universally fail health check requests
  regardless of how the filter is configured (pass through, etc.).

.. _operations_admin_interface_healthcheck_ok:

.. http:post:: /healthcheck/ok

  Negate the effect of :http:post:`/healthcheck/fail`. This requires the use of the HTTP
  :ref:`health check filter <config_http_filters_health_check>`.

.. http:get:: /hot_restart_version

  See :option:`--hot-restart-version`.

.. _operations_admin_interface_logging:

.. http:post:: /logging

  Enable/disable different logging levels on different subcomponents. Generally only used during
  development.

.. http:post:: /quitquitquit

  Cleanly exit the server.

.. http:post:: /reset_counters

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

.. _operations_admin_interface_stats:

.. http:get:: /stats

  Outputs all statistics on demand. This command is very useful for local debugging.
  Histograms will output the computed quantiles i.e P0,P25,P50,P75,P90,P99,P99.9 and P100.
  The output for each quantile will be in the form of (interval,cumulative) where interval value
  represents the summary since last flush interval and cumulative value represents the
  summary since the start of envoy instance.
  See :ref:`here <operations_stats>` for more information.

  .. http:get:: /stats?format=json

  Outputs /stats in JSON format. This can be used for programmatic access of stats. Counters and Gauges
  will be in the form of a set of (name,value) pairs. Histograms will be under the element "histograms",
  that contains "supported_quantiles" which lists the quantiles supported and an array of computed_quantiles
  that has the computed quantile for each histogram. Only histograms with recorded values will be exported.

  If a histogram is not updated during an interval, the ouput will have null for all the quantiles.
  
  Example histogram output:

.. code-block:: json

  {
    "histograms": {
      "supported_quantiles": [
        0, 25, 50, 75, 90, 95, 99, 99.9, 100
      ],
      "computed_quantiles": [
        {
          "name": "cluster.external_auth_cluster.upstream_cx_length_ms",
          "values": [
            {"interval": 0, "cumulative": 0},
            {"interval": 0, "cumulative": 0},
            {"interval": 1.0435787, "cumulative": 1.0435787},
            {"interval": 1.0941565, "cumulative": 1.0941565},
            {"interval": 2.0860023, "cumulative": 2.0860023},
            {"interval": 3.0665233, "cumulative": 3.0665233},
            {"interval": 6.046609, "cumulative": 6.046609},
            {"interval": 229.57333,"cumulative": 229.57333},
            {"interval": 260,"cumulative": 260}
          ]
        },
        {
          "name": "http.admin.downstream_rq_time",
          "values": [
            {"interval": null, "cumulative": 0},
            {"interval": null, "cumulative": 0},
            {"interval": null, "cumulative": 1.0435787},
            {"interval": null, "cumulative": 1.0941565},
            {"interval": null, "cumulative": 2.0860023},
            {"interval": null, "cumulative": 3.0665233},
            {"interval": null, "cumulative": 6.046609},
            {"interval": null, "cumulative": 229.57333},
            {"interval": null, "cumulative": 260}
          ]
        }
      ]
    }
  }

.. http:get:: /stats?format=prometheus

  or alternatively,

  .. http:get:: /stats/prometheus

  Outputs /stats in `Prometheus <https://prometheus.io/docs/instrumenting/exposition_formats/>`_
  v0.0.4 format. This can be used to integrate with a Prometheus server. Currently, only counters and
  gauges are output. Histograms will be output in a future update.

.. _operations_admin_interface_runtime:

.. http:get:: /runtime

  Outputs all runtime values on demand in JSON format. See :ref:`here <arch_overview_runtime>` for
  more information on how these values are configured and utilized. The output include the list of
  the active runtime override layers and the stack of layer values for each key. Empty strings
  indicate no value, and the final active value from the stack also is included in a separate key.
  Example output:

.. code-block:: json

  {
    "layers": [
      "disk",
      "override",
      "admin",
    ],
    "entries": {
      "my_key": {
        "layer_values": [
          "my_disk_value",
          "",
          ""
        ],
        "final_value": "my_disk_value"
      },
      "my_second_key": {
        "layer_values": [
          "my_second_disk_value",
          "my_disk_override_value",
          "my_admin_override_value"
        ],
        "final_value": "my_admin_override_value"
      }
    }
  }

.. _operations_admin_interface_runtime_modify:

.. http:post:: /runtime_modify?key1=value1&key2=value2&keyN=valueN

  Adds or modifies runtime values as passed in query parameters. To delete a previously added key,
  use an empty string as the value. Note that deletion only applies to overrides added via this
  endpoint; values loaded from disk can be modified via override but not deleted.

.. attention::

  Use the /runtime_modify endpoint with care. Changes are effectively immediately. It is
  **critical** that the admin interface is :ref:`properly secured
  <operations_admin_interface_security>`.
