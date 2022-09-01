.. _operations_admin_interface:

Administration interface
========================

Envoy exposes a local administration interface that can be used to query and
modify different aspects of the server:

* :ref:`v3 API reference <envoy_v3_api_msg_config.bootstrap.v3.Admin>`

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
      profile_path: /tmp/envoy.prof
      address:
        socket_address: { address: 127.0.0.1, port_value: 9901 }

  In the future additional security options will be added to the administration interface. This
  work is tracked in `this <https://github.com/envoyproxy/envoy/issues/2763>`_ issue.

  All mutations must be sent as HTTP POST operations. When a mutation is requested via GET,
  the request has no effect, and an HTTP 400 (Invalid Request) response is returned.

.. note::

  For an endpoint with *?format=json*, it dumps data as a JSON-serialized proto. Fields with default
  values are not rendered. For example for */clusters?format=json*, the circuit breakers thresholds
  priority field is omitted when its value is :ref:`DEFAULT priority
  <envoy_v3_api_enum_value_config.core.v3.RoutingPriority.DEFAULT>` as shown below:

  .. code-block:: json

    {
     "thresholds": [
      {
       "max_connections": 1,
       "max_pending_requests": 1024,
       "max_requests": 1024,
       "max_retries": 1
      },
      {
       "priority": "HIGH",
       "max_connections": 1,
       "max_pending_requests": 1024,
       "max_requests": 1024,
       "max_retries": 1
      }
     ]
    }

.. http:get:: /

  Render an HTML home page with a table of links to all available options. This can be
  disabled by compiling Envoy with ``--define=admin_html=disabled`` in which case an error
  message is printed. Disabling the HTML mode reduces the Envoy binary size.

.. http:get:: /help

  Print a textual table of all available options.

.. _operations_admin_interface_certs:

.. http:get:: /certs

  List out all loaded TLS certificates, including file name, serial number, subject alternate names and days until
  expiration in JSON format conforming to the :ref:`certificate proto definition <envoy_v3_api_msg_admin.v3.Certificates>`.

.. _operations_admin_interface_clusters:

.. http:get:: /clusters

  List out all configured :ref:`cluster manager <arch_overview_cluster_manager>` clusters. This
  information includes all discovered upstream hosts in each cluster along with per host statistics.
  This is useful for debugging service discovery issues.

  Cluster manager information
    - ``version_info`` string -- the version info string of the last loaded
      :ref:`CDS<config_cluster_manager_cds>` update.
      If Envoy does not have :ref:`CDS<config_cluster_manager_cds>` setup, the
      output will read ``version_info::static``.

  Cluster wide information
    - :ref:`circuit breakers<config_cluster_manager_cluster_circuit_breakers>` settings for all priority settings.

    - Information about :ref:`outlier detection<arch_overview_outlier_detection>` if a detector is installed. Currently
      :ref:`average success rate <envoy_v3_api_field_data.cluster.v3.OutlierEjectSuccessRate.cluster_average_success_rate>`,
      and :ref:`ejection threshold<envoy_v3_api_field_data.cluster.v3.OutlierEjectSuccessRate.cluster_success_rate_ejection_threshold>`
      are presented. Both of these values could be ``-1`` if there was not enough data to calculate them in the last
      :ref:`interval<envoy_v3_api_field_config.cluster.v3.OutlierDetection.interval>`.

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
      :ref:`request volume<envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_request_volume>`
      in the :ref:`interval<envoy_v3_api_field_config.cluster.v3.OutlierDetection.interval>`
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

.. http:get:: /clusters?format=json

  Dump the */clusters* output in a JSON-serialized proto. See the
  :ref:`definition <envoy_v3_api_msg_admin.v3.Clusters>` for more information.

.. _operations_admin_interface_config_dump:

.. http:get:: /config_dump

  Dump currently loaded configuration from various Envoy components as JSON-serialized proto
  messages. See the :ref:`response definition <envoy_v3_api_msg_admin.v3.ConfigDump>` for more
  information.

.. warning::
  Configuration may include :ref:`TLS certificates <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.TlsCertificate>`. Before
  dumping the configuration, Envoy will attempt to redact the ``private_key`` and ``password``
  fields from any certificates it finds. This relies on the configuration being a strongly-typed
  protobuf message. If your Envoy configuration uses deprecated ``config`` fields (of type
  ``google.protobuf.Struct``), please update to the recommended ``typed_config`` fields (of type
  ``google.protobuf.Any``) to ensure sensitive data is redacted properly.

.. warning::
  The underlying proto is marked v2alpha and hence its contents, including the JSON representation,
  are not guaranteed to be stable.

.. _operations_admin_interface_config_dump_include_eds:

.. http:get:: /config_dump?include_eds

  Dump currently loaded configuration including EDS. See the :ref:`response definition <envoy_v3_api_msg_admin.v3.EndpointsConfigDump>` for more
  information.

.. _operations_admin_interface_config_dump_by_mask:

.. http:get:: /config_dump?mask={}

  Specify a subset of fields that you would like to be returned. The mask is parsed as a
  ``ProtobufWkt::FieldMask`` and applied to each top level dump such as
  :ref:`BootstrapConfigDump <envoy_v3_api_msg_admin.v3.BootstrapConfigDump>` and
  :ref:`ClustersConfigDump <envoy_v3_api_msg_admin.v3.ClustersConfigDump>`.
  This behavior changes if both resource and mask query parameters are specified. See
  below for details.

.. _operations_admin_interface_config_dump_by_resource:

.. http:get:: /config_dump?resource={}

  Dump only the currently loaded configuration that matches the specified resource. The resource must
  be a repeated field in one of the top level config dumps such as
  :ref:`static_listeners <envoy_v3_api_field_admin.v3.ListenersConfigDump.static_listeners>` from
  :ref:`ListenersConfigDump <envoy_v3_api_msg_admin.v3.ListenersConfigDump>` or
  :ref:`dynamic_active_clusters <envoy_v3_api_field_admin.v3.ClustersConfigDump.dynamic_active_clusters>` from
  :ref:`ClustersConfigDump <envoy_v3_api_msg_admin.v3.ClustersConfigDump>`. If you need a non-repeated
  field, use the mask query parameter documented above. If you want only a subset of fields from the repeated
  resource, use both as documented below.

.. _operations_admin_interface_config_dump_by_name_regex:

.. http:get:: /config_dump?name_regex={}

  Dump only the currently loaded configurations whose names match the specified regex. Can be used with
  both ``resource`` and ``mask`` query parameters.

  For example, ``/config_dump?name_regex=.*substring.*`` would return all resource types
  whose name field matches the given regex.

  Per resource, the matched name field is:

  - :ref:`envoy.config.listener.v3.Listener.name <envoy_v3_api_field_config.listener.v3.Listener.name>`
  - :ref:`envoy.config.route.v3.RouteConfiguration.name <envoy_v3_api_field_config.route.v3.RouteConfiguration.name>`
  - :ref:`envoy.config.route.v3.ScopedRouteConfiguration.name <envoy_v3_api_field_config.route.v3.ScopedRouteConfiguration.name>`
  - :ref:`envoy.config.cluster.v3.Cluster.name <envoy_v3_api_field_config.cluster.v3.Cluster.name>`
  - :ref:`envoy.extensions.transport_sockets.tls.v3.Secret <envoy_v3_api_field_extensions.transport_sockets.tls.v3.Secret.name>`
  - :ref:`envoy.config.endpoint.v3.ClusterLoadAssignment <envoy_v3_api_field_config.endpoint.v3.ClusterLoadAssignment.cluster_name>`

.. _operations_admin_interface_config_dump_by_resource_and_mask:

.. http:get:: /config_dump?resource={}&mask={}

  When both resource and mask query parameters are specified, the mask is applied to every element
  in the desired repeated field so that only a subset of fields are returned. The mask is parsed
  as a ``ProtobufWkt::FieldMask``.

  For example, get the names of all active dynamic clusters with
  ``/config_dump?resource=dynamic_active_clusters&mask=cluster.name``

.. http:get:: /contention

  Dump current Envoy mutex contention stats (:ref:`MutexStats <envoy_v3_api_msg_admin.v3.MutexStats>`) in JSON
  format, if mutex tracing is enabled. See :option:`--enable-mutex-tracing`.

.. http:post:: /cpuprofiler

  Enable or disable the CPU profiler. Requires compiling with gperftools. The output file can be configured by admin.profile_path.

.. http:post:: /heapprofiler

  Enable or disable the Heap profiler. Requires compiling with gperftools. The output file can be configured by admin.profile_path.

.. _operations_admin_interface_heap_dump:

.. http:get:: /heap_dump

  Dump current heap profile of Envoy process. The output content is parsable binary by the ``pprof`` tool.
  Requires compiling with tcmalloc (default).

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

.. _operations_admin_interface_init_dump:

.. http:get:: /init_dump

  Dump current information of unready targets of various Envoy components as JSON-serialized proto
  messages. See the :ref:`response definition <envoy_v3_api_msg_admin.v3.UnreadyTargetsDumps>` for more
  information.

.. _operations_admin_interface_init_dump_by_mask:

.. http:get:: /init_dump?mask={}

  When mask query parameters is specified, the mask value is the desired component to dump unready targets.
  The mask is parsed as a ``ProtobufWkt::FieldMask``.

  For example, get the unready targets of all listeners with
  ``/init_dump?mask=listener``

.. _operations_admin_interface_listeners:

.. http:get:: /listeners

  List out all configured :ref:`listeners <arch_overview_listeners>`. This information includes the names of listeners as well as
  the addresses that they are listening on. If a listener is configured to listen on port 0, then the output will contain the actual
  port that was allocated by the OS.

.. http:get:: /listeners?format=json

  Dump the */listeners* output in a JSON-serialized proto. See the
  :ref:`definition <envoy_v3_api_msg_admin.v3.Listeners>` for more information.

.. _operations_admin_interface_logging:

.. http:post:: /logging

  Enable/disable logging levels for different loggers.

  - To change the logging level across all loggers, set the query parameter as level=<desired_level>.
  - To change a particular logger's level, set the query parameter like so, <logger_name>=<desired_level>.
  - To change multiple logging levels at once, set the query parameter as paths=<logger_name1>=<desired_level1>,<logger_name2>=<desired_level2>.
  - To list the loggers, send a POST request to the /logging endpoint without a query parameter.

  .. note::

    Generally only used during development. With ``--enable-fine-grain-logging`` being set, the logger is represented
    by the path of the file it belongs to (to be specific, the path determined by ``__FILE__``), so the logger list
    will show a list of file paths, and the specific path should be used as <logger_name> to change the log level.

.. http:get:: /memory

  Prints current memory allocation / heap usage, in bytes. Useful in lieu of printing all ``/stats`` and filtering to get the memory-related statistics.

.. http:post:: /quitquitquit

  Cleanly exit the server.

.. http:post:: /reset_counters

  Reset all counters to zero. This is useful along with :http:get:`/stats` during debugging. Note
  that this does not drop any data sent to statsd. It just affects local output of the
  :http:get:`/stats` command.

.. _operations_admin_interface_drain:

.. http:post:: /drain_listeners

   :ref:`Drains <arch_overview_draining>` all listeners.

   .. http:post:: /drain_listeners?inboundonly

   :ref:`Drains <arch_overview_draining>` all inbound listeners. ``traffic_direction`` field in
   :ref:`Listener <envoy_v3_api_msg_config.listener.v3.Listener>` is used to determine whether a listener
   is inbound or outbound.

   .. http:post:: /drain_listeners?graceful

   When draining listeners, enter a graceful drain period prior to closing listeners.
   This behaviour and duration is configurable via server options or CLI
   (:option:`--drain-time-s` and :option:`--drain-strategy`).

.. attention::

   This operation directly stops the matched listeners on workers. Once listeners in a given
   traffic direction are stopped, listener additions and modifications in that direction
   are not allowed.

.. _operations_admin_interface_server_info:

.. http:get:: /server_info

  Outputs a JSON message containing information about the running server.

  Sample output looks like:

  .. code-block:: json

    {
      "version": "b050513e840aa939a01f89b07c162f00ab3150eb/1.9.0-dev/Modified/DEBUG",
      "state": "LIVE",
      "command_line_options": {
        "base_id": "0",
        "concurrency": 8,
        "config_path": "config.yaml",
        "config_yaml": "",
        "allow_unknown_static_fields": false,
        "admin_address_path": "",
        "local_address_ip_version": "v4",
        "log_level": "info",
        "component_log_level": "",
        "log_format": "[%Y-%m-%d %T.%e][%t][%l][%n] %v",
        "log_path": "",
        "hot_restart_version": false,
        "service_cluster": "",
        "service_node": "",
        "service_zone": "",
        "mode": "Serve",
        "disable_hot_restart": false,
        "enable_mutex_tracing": false,
        "restart_epoch": 0,
        "file_flush_interval": "10s",
        "drain_time": "600s",
        "parent_shutdown_time": "900s",
        "cpuset_threads": false
      },
      "uptime_current_epoch": "6s",
      "uptime_all_epochs": "6s",
      "node": {
        "id": "node1",
        "cluster": "cluster1",
        "user_agent_name": "envoy",
        "user_agent_build_version": {
          "version": {
            "major_number": 1,
            "minor_number": 15,
            "patch": 0
          }
        },
        "metadata": {},
        "extensions": [],
        "client_features": [],
        "listening_addresses": []
      }
    }

  See the :ref:`ServerInfo proto <envoy_v3_api_msg_admin.v3.ServerInfo>` for an
  explanation of the output.

.. http:get:: /ready

  Outputs a string and error code reflecting the state of the server. 200 is returned for the LIVE state,
  and 503 otherwise. This can be used as a readiness check.

  Example output:

  .. code-block:: none

    LIVE

  See the ``state`` field of the :ref:`ServerInfo proto <envoy_v3_api_msg_admin.v3.ServerInfo>` for an
  explanation of the output.

.. _operations_admin_interface_stats:

.. http:get:: /stats

  Outputs all statistics on demand. This command is very useful for local debugging.
  Histograms will output the computed quantiles i.e P0,P25,P50,P75,P90,P99,P99.9 and P100.
  The output for each quantile will be in the form of (interval,cumulative) where the interval value
  represents the summary since last flush. By default, a timer is setup to flush in intervals
  defined by :ref:`stats_flush_interval <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.stats_flush_interval>`,
  defaulting to 5 seconds. If :ref:`stats_flush_on_admin <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.stats_flush_on_admin>`
  is specified, stats are flushed when this endpoint is queried and a timer will not be used. The cumulative
  value represents the summary since the start of Envoy instance. "No recorded values" in the histogram
  output indicates that it has not been updated with a value. See :ref:`here <operations_stats>` for more information.

  .. http:get:: /stats?usedonly

  Outputs statistics that Envoy has updated (counters incremented at least once, gauges changed at
  least once, and histograms added to at least once).

  .. http:get:: /stats?filter=regex

  Filters the returned stats to those with names matching the regular
  expression ``regex``. Compatible with ``usedonly``. Performs partial
  matching by default, so ``/stats?filter=server`` will return all stats
  containing the word ``server``.  Full-string matching can be specified
  with begin- and end-line anchors. (i.e.  ``/stats?filter=^server.concurrency$``)

  By default, the regular expression is evaluated using the
  `Google RE2 <https://github.com/google/re2>`_ engine. To switch
  to std::regex using Ecmascript syntax, POST an admin :ref:`runtime <arch_overview_runtime>` request:
  ``/runtime_modify?envoy.reloadable_features.admin_stats_filter_use_re2=false``

  .. http:get:: /stats?histogram_buckets=cumulative

  Changes histogram output to display cumulative buckets with upper bounds (e.g. B0.5, B1, B5, ...).
  The output for each bucket will be in the form of (interval,cumulative) (e.g. B0.5(0,0)).
  All values below the upper bound are included even if they are placed into other buckets.
  Compatible with ``usedonly`` and ``filter``.

  .. http:get:: /stats?histogram_buckets=disjoint

  Changes histogram output to display disjoint buckets with upper bounds (e.g. B0.5, B1, B5, ...).
  The output for each bucket will be in the form of (interval,cumulative) (e.g. B0.5(0,0)).
  Buckets do not include values from other buckets with smaller upper bounds;
  the previous bucket's upper bound acts as a lower bound. Compatible with ``usedonly`` and ``filter``.

.. http:get:: /stats?format=json

  Outputs /stats in JSON format. This can be used for programmatic access of stats. Counters and Gauges
  will be in the form of a set of (name,value) pairs. Histograms will be under the element "histograms",
  that contains "supported_quantiles" which lists the quantiles supported and an array of computed_quantiles
  that has the computed quantile for each histogram.

  If a histogram is not updated during an interval, the output will have null for all the quantiles.

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

  .. http:get:: /stats?format=json&usedonly

  Outputs statistics that Envoy has updated (counters incremented at least once,
  gauges changed at least once, and histograms added to at least once) in JSON format.

  .. http:get:: /stats?format=json&histogram_buckets=cumulative

  Changes histogram output to display cumulative buckets with upper bounds.
  All values below the upper bound are included even if they are placed into other buckets.
  Compatible with ``usedonly`` and ``filter``.

  Example histogram output:

  .. code-block:: json

    {
      "histograms": [
        {
          "name": "example_histogram",
          "buckets": [
            {"upper_bound": 1, "interval": 0, "cumulative": 0},
            {"upper_bound": 2, "interval": 0, "cumulative": 1},
            {"upper_bound": 3, "interval": 1, "cumulative": 3},
            {"upper_bound": 4, "interval": 1, "cumulative": 3}
          ]
        },
        {
          "name": "other_example_histogram",
          "buckets": [
            {"upper_bound": 0.5, "interval": 0, "cumulative": 0},
            {"upper_bound": 1, "interval": 0, "cumulative": 0},
            {"upper_bound": 5, "interval": 0, "cumulative": 0},
            {"upper_bound": 10, "interval": 0, "cumulative": 0},
            {"upper_bound": 25, "interval": 0, "cumulative": 0},
            {"upper_bound": 50, "interval": 0, "cumulative": 0},
            {"upper_bound": 100, "interval": 0, "cumulative": 0},
            {"upper_bound": 250, "interval": 0, "cumulative": 0},
            {"upper_bound": 500, "interval": 0, "cumulative": 0},
            {"upper_bound": 1000, "interval": 0, "cumulative": 0},
            {"upper_bound": 2500, "interval": 0, "cumulative": 100},
            {"upper_bound": 5000, "interval": 0, "cumulative": 300},
            {"upper_bound": 10000, "interval": 0, "cumulative": 600},
            {"upper_bound": 30000, "interval": 0, "cumulative": 600},
            {"upper_bound": 60000, "interval": 0, "cumulative": 600},
            {"upper_bound": 300000, "interval": 0, "cumulative": 600},
            {"upper_bound": 600000, "interval": 0, "cumulative": 600},
            {"upper_bound": 1800000, "interval": 0, "cumulative": 600},
            {"upper_bound": 3600000, "interval": 0, "cumulative": 600}
          ]
        }
      ]
    }

  .. http:get:: /stats?format=json&histogram_buckets=disjoint

  Changes histogram output to display disjoint buckets with upper bounds.
  Buckets do not include values from other buckets with smaller upper bounds;
  the previous bucket's upper bound acts as a lower bound. Compatible with ``usedonly`` and ``filter``.

  Example histogram output:

  .. code-block:: json

    {
      "histograms": [
        {
          "name": "example_histogram",
          "buckets": [
            {"upper_bound": 1, "interval": 0, "cumulative": 0},
            {"upper_bound": 2, "interval": 0, "cumulative": 1},
            {"upper_bound": 3, "interval": 1, "cumulative": 2},
            {"upper_bound": 4, "interval": 0, "cumulative": 0}
          ]
        },
        {
          "name": "other_example_histogram",
          "buckets": [
            {"upper_bound": 0.5, "interval": 0, "cumulative": 0},
            {"upper_bound": 1, "interval": 0, "cumulative": 0},
            {"upper_bound": 5, "interval": 0, "cumulative": 0},
            {"upper_bound": 10, "interval": 0, "cumulative": 0},
            {"upper_bound": 25, "interval": 0, "cumulative": 0},
            {"upper_bound": 50, "interval": 0, "cumulative": 0},
            {"upper_bound": 100, "interval": 0, "cumulative": 0},
            {"upper_bound": 250, "interval": 0, "cumulative": 0},
            {"upper_bound": 500, "interval": 0, "cumulative": 0},
            {"upper_bound": 1000, "interval": 0, "cumulative": 0},
            {"upper_bound": 2500, "interval": 0, "cumulative": 100},
            {"upper_bound": 5000, "interval": 0, "cumulative": 200},
            {"upper_bound": 10000, "interval": 0, "cumulative": 0},
            {"upper_bound": 30000, "interval": 0, "cumulative": 0},
            {"upper_bound": 60000, "interval": 0, "cumulative": 0},
            {"upper_bound": 300000, "interval": 0, "cumulative": 0},
            {"upper_bound": 600000, "interval": 0, "cumulative": 0},
            {"upper_bound": 1800000, "interval": 0, "cumulative": 0},
            {"upper_bound": 3600000, "interval": 0, "cumulative": 0}
          ]
        }
      ]
    }

.. http:get:: /stats?format=prometheus

  or alternatively,

  .. http:get:: /stats/prometheus

  Outputs /stats in `Prometheus <https://prometheus.io/docs/instrumenting/exposition_formats/>`_
  v0.0.4 format. This can be used to integrate with a Prometheus server.

  .. http:get:: /stats?format=prometheus&usedonly

  You can optionally pass the ``usedonly`` URL query parameter to only get statistics that
  Envoy has updated (counters incremented at least once, gauges changed at least once,
  and histograms added to at least once).

  .. http:get:: /stats?format=prometheus&text_readouts

  Optional ``text_readouts`` query parameter is used to get all stats including text readouts.
  Text readout stats are returned in gauge format. These gauges always have value 0. Each
  gauge record has additional label named ``text_value`` that contains value of a text readout.

  .. warning::
    Every unique combination of key-value label pair represents a new time series
    in Prometheus, which can dramatically increase the amount of data stored.
    Text readout stats create a new label value every time the value
    of the text readout stat changes, which could create an unbounded number of time series.

.. http:get:: /stats/recentlookups

  This endpoint helps Envoy developers debug potential contention
  issues in the stats system. Initially, only the count of StatName
  lookups is acumulated, not the specific names that are being looked
  up. In order to see specific recent requests, you must enable the
  feature by POSTing to ``/stats/recentlookups/enable``. There may be
  approximately 40-100 nanoseconds of added overhead per lookup.

  When enabled, this endpoint emits a table of stat names that were
  recently accessed as strings by Envoy. Ideally, strings should be
  converted into StatNames, counters, gauges, and histograms by Envoy
  code only during startup or when receiving a new configuration via
  xDS. This is because when stats are looked up as strings they must
  take a global symbol table lock. During startup this is acceptable,
  but in response to user requests on high core-count machines, this
  can cause performance issues due to mutex contention.

  See :repo:`source/docs/stats.md` for more details.

  Note also that actual mutex contention can be tracked via :http:get:`/contention`.

  .. http:post:: /stats/recentlookups/enable

  Turns on collection of recent lookup of stat-names, thus enabling
  ``/stats/recentlookups``.

  See :repo:`source/docs/stats.md` for more details.

  .. http:post:: /stats/recentlookups/disable

  Turns off collection of recent lookup of stat-names, thus disabling
  ``/stats/recentlookups``. It also clears the list of lookups. However,
  the total count, visible as stat ``server.stats_recent_lookups``, is
  not cleared, and continues to accumulate.

  See :repo:`source/docs/stats.md` for more details.

  .. http:post:: /stats/recentlookups/clear

  Clears all outstanding lookups and counts. This clears all recent
  lookups data as well as the count, but collection continues if
  it is enabled.

  See :repo:`source/docs/stats.md` for more details.

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

  .. _operations_admin_interface_hystrix_event_stream:

.. http:get:: /hystrix_event_stream

  This endpoint is intended to be used as the stream source for
  `Hystrix dashboard <https://github.com/Netflix-Skunkworks/hystrix-dashboard/wiki>`_.
  a GET to this endpoint will trigger a stream of statistics from Envoy in
  `text/event-stream <https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events>`_
  format, as expected by the Hystrix dashboard.

  If invoked from a browser or a terminal, the response will be shown as a continuous stream,
  sent in intervals defined by the :ref:`Bootstrap <envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>`
  :ref:`stats_flush_interval <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.stats_flush_interval>`

  This handler is enabled only when a Hystrix sink is enabled in the config file as documented
  :ref:`here <envoy_v3_api_msg_config.metrics.v3.HystrixSink>`.

  As Envoy's and Hystrix resiliency mechanisms differ, some of the statistics shown in the dashboard
  had to be adapted:

  * **Thread pool rejections** - Generally similar to what's called short circuited in Envoy,
    and counted by *upstream_rq_pending_overflow*, although the term thread pool is not accurate for
    Envoy. Both in Hystrix and Envoy, the result is rejected requests which are not passed upstream.
  * **circuit breaker status (closed or open)** - Since in Envoy, a circuit is opened based on the
    current number of connections/requests in queue, there is no sleeping window for circuit breaker,
    circuit open/closed is momentary. Hence, we set the circuit breaker status to "forced closed".
  * **Short-circuited (rejected)** - The term exists in Envoy but refers to requests not sent because
    of passing a limit (queue or connections), while in Hystrix it refers to requests not sent because
    of high percentage of service unavailable responses during some time frame.
    In Envoy, service unavailable response will cause **outlier detection** - removing a node off the
    load balancer pool, but requests are not rejected as a result. Therefore, this counter is always
    set to '0'.
  * Latency information represents data since last flush.
    Mean latency is currently not available.

.. http:post:: /tap

  This endpoint is used for configuring an active tap session. It is only
  available if a valid tap extension has been configured, and that extension has
  been configured to accept admin configuration. See:

  * :ref:`HTTP tap filter configuration <config_http_filters_tap_admin_handler>`

.. http:post:: /reopen_logs

  Triggers reopen of all access logs. Behavior is similar to SIGUSR1 handling.
