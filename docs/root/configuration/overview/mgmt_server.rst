Management Server
-----------------

.. _config_overview_mgmt_con_issues:

Management Server Unreachability
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When an Envoy instance loses connectivity with the management server, Envoy will latch on to
the previous configuration while actively retrying in the background to reestablish the
connection with the management server.

It is important that Envoy detects when a connection to a management server is unhealthy so that
it can try to establish a new connection. Configuring either
:ref:`TCP keep-alives <envoy_v3_api_field_config.cluster.v3.UpstreamConnectionOptions.tcp_keepalive>`
or :ref:`HTTP/2 keepalives <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.connection_keepalive>`
in the cluster that connects to the management server is recommended.

Envoy debug logs the fact that it is not able to establish a connection with the management server
every time it attempts a connection.

:ref:`connected_state <management_server_stats>` statistic provides a signal for monitoring this behavior.

.. _management_server_stats:

Statistics
^^^^^^^^^^

Management Server has a statistics tree rooted at *control_plane.* with the following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   connected_state, Gauge, A boolean (1 for connected and 0 for disconnected) that indicates the current connection state with management server
   rate_limit_enforced, Counter, Total number of times rate limit was enforced for management server requests
   pending_requests, Gauge, Total number of pending requests when the rate limit was enforced
   identifier, TextReadout, The identifier of the control plane instance that sent the last discovery response

.. _subscription_statistics:

xDS subscription statistics
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Envoy discovers its various dynamic resources via discovery
services referred to as *xDS*. Resources are requested via :ref:`subscriptions <xds_protocol>`,
by specifying a filesystem path to watch, initiating gRPC streams or polling a REST-JSON URL.

The following statistics are generated for all subscriptions.

.. csv-table::
 :header: Name, Type, Description
 :widths: 1, 1, 2

 config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
 config_reload_time_ms, Gauge, Timestamp of the last config reload as milliseconds since the epoch
 init_fetch_timeout, Counter, Total :ref:`initial fetch timeouts <envoy_v3_api_field_config.core.v3.ConfigSource.initial_fetch_timeout>`
 update_attempt, Counter, Total API fetches attempted
 update_success, Counter, Total API fetches completed successfully
 update_failure, Counter, Total API fetches that failed because of network errors
 update_rejected, Counter, Total API fetches that failed because of schema/validation errors
 update_time, Gauge, Timestamp of the last successful API fetch attempt as milliseconds since the epoch. Refreshed even after a trivial configuration reload that contained no configuration changes.
 version, Gauge, Hash of the contents from the last successful API fetch
 version_text, TextReadout, The version text from the last successful API fetch
 control_plane.connected_state, Gauge, A boolean (1 for connected and 0 for disconnected) that indicates the current connection state with management server
