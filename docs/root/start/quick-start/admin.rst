.. _start_quick_start_admin:

Envoy admin interface
=====================

The optional admin interface provided by Envoy allows you to dump configuration and statistics, change the
behaviour of the server, and tap traffic according to specific filter rules.

The admin interface can be configured for static and dynamic setups.

Enabling the :ref:`admin <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.admin>` interface with
dynamic configuration can be particularly useful as it allows you to use the
:ref:`config_dump <start_quick_start_admin_config_dump>` endpoint to see how Envoy is configured at
a particular point in time.

This guide provides configuration information, and some basic examples of using a couple of the admin
endpoints.

See the :ref:`admin docs <operations_admin_interface>` for information on all of the available endpoints.

Some of the examples below make use of the `jq <https://stedolan.github.io/jq/>`_ tool to parse the output
from the admin server.

.. _start_quick_start_admin_config:

``admin``
---------

The :ref:`admin message <envoy_v3_api_msg_config.bootstrap.v3.Admin>` is required to enable and configure
the administration server.

The ``address`` key specifies the listening :ref:`address <envoy_v3_api_file_envoy/config/core/v3/address.proto>`
which in the demo configuration is ``0.0.0.0:9901``.

You must set the ``access_log_path`` to specify where to send access logs.

In this example, the logs are simply discarded.

.. code-block:: yaml
   :emphasize-lines: 2, 5-6

   admin:
     access_log_path: /dev/null
     address:
       socket_address:
         address: 0.0.0.0
	 port_value: 9901

.. warning::

   You may wish to restrict the network address the admin server listens to in your own deployment.

.. _start_quick_start_admin_config_dump:

Admin endpoints: ``config_dump``
--------------------------------

The :ref:`config_dump <operations_admin_interface_config_dump>` endpoint dumps Envoy's configuration
in ``json`` format.

The following command allows you to see the types of config available:

.. code-block:: console

   $ curl -s http://localhost:9901/config_dump | jq -r '.configs[] | .["@type"]'
   type.googleapis.com/envoy.admin.v3.BootstrapConfigDump
   type.googleapis.com/envoy.admin.v3.ClustersConfigDump
   type.googleapis.com/envoy.admin.v3.ListenersConfigDump
   type.googleapis.com/envoy.admin.v3.ScopedRoutesConfigDump
   type.googleapis.com/envoy.admin.v3.RoutesConfigDump
   type.googleapis.com/envoy.admin.v3.SecretsConfigDump

To dump the ``socket_address`` of the first ``dynamic_listener`` currently configured, you could:

.. code-block:: console

   $ curl -s http://localhost:19000/config_dump?resource=dynamic_listeners | jq '.configs[0].active_state.listener.address'
   {
     "socket_address": {
       "address": "0.0.0.0",
       "port_value": 10000
     }
   }

See the reference section for :ref:`config_dump <operations_admin_interface_config_dump>` for further information
on available parameters and responses.

.. _start_quick_start_admin_stats:

Admin endpoints: ``stats``
--------------------------

The admin stats endpoint allows you to retrieve runtime information about Envoy.

The stats are provided as ``key: value`` pairs, where the keys use a hierarchical dotted notation,
and the values are one of ``counter``, ``histogram`` or ``gauge`` types.

To see the top-level categories of stats available, you can:

.. code-block:: console

   $ curl -s http://localhost:9901/stats | cut -d. -f1 | sort | uniq
   cluster
   cluster_manager
   filesystem
   http
   http1
   listener
   listener_manager
   main_thread
   runtime
   server
   vhost
   workers

The stats endpoint accepts a ``filter`` ``regex`` argument:

.. code-block:: console

   $ curl -s http://localhost:19000/stats?filter='^http\.ingress_http'
   http.ingress_http.downstream_cx_active: 0
   http.ingress_http.downstream_cx_delayed_close_timeout: 0
   http.ingress_http.downstream_cx_destroy: 3
   http.ingress_http.downstream_cx_destroy_active_rq: 0
   http.ingress_http.downstream_cx_destroy_local: 0
   http.ingress_http.downstream_cx_destroy_local_active_rq: 0
   http.ingress_http.downstream_cx_destroy_remote: 3
   http.ingress_http.downstream_cx_destroy_remote_active_rq: 0
   http.ingress_http.downstream_cx_drain_close: 0
   http.ingress_http.downstream_cx_http1_active: 0
   http.ingress_http.downstream_cx_http1_total: 3
   http.ingress_http.downstream_cx_http2_active: 0
   http.ingress_http.downstream_cx_http2_total: 0
   http.ingress_http.downstream_cx_http3_active: 0
   http.ingress_http.downstream_cx_http3_total: 0
   http.ingress_http.downstream_cx_idle_timeout: 0
   http.ingress_http.downstream_cx_max_duration_reached: 0
   http.ingress_http.downstream_cx_overload_disable_keepalive: 0
   http.ingress_http.downstream_cx_protocol_error: 0
   http.ingress_http.downstream_cx_rx_bytes_buffered: 0
   http.ingress_http.downstream_cx_rx_bytes_total: 250
   http.ingress_http.downstream_cx_ssl_active: 0
   http.ingress_http.downstream_cx_ssl_total: 0
   http.ingress_http.downstream_cx_total: 3
   http.ingress_http.downstream_cx_tx_bytes_buffered: 0
   http.ingress_http.downstream_cx_tx_bytes_total: 1117
   http.ingress_http.downstream_cx_upgrades_active: 0
   http.ingress_http.downstream_cx_upgrades_total: 0
   http.ingress_http.downstream_flow_control_paused_reading_total: 0
   http.ingress_http.downstream_flow_control_resumed_reading_total: 0
   http.ingress_http.downstream_rq_1xx: 0
   http.ingress_http.downstream_rq_2xx: 3
   http.ingress_http.downstream_rq_3xx: 0
   http.ingress_http.downstream_rq_4xx: 0
   http.ingress_http.downstream_rq_5xx: 0
   http.ingress_http.downstream_rq_active: 0
   http.ingress_http.downstream_rq_completed: 3
   http.ingress_http.downstream_rq_http1_total: 3
   http.ingress_http.downstream_rq_http2_total: 0
   http.ingress_http.downstream_rq_http3_total: 0
   http.ingress_http.downstream_rq_idle_timeout: 0
   http.ingress_http.downstream_rq_max_duration_reached: 0
   http.ingress_http.downstream_rq_non_relative_path: 0
   http.ingress_http.downstream_rq_overload_close: 0
   http.ingress_http.downstream_rq_response_before_rq_complete: 0
   http.ingress_http.downstream_rq_rx_reset: 0
   http.ingress_http.downstream_rq_timeout: 0
   http.ingress_http.downstream_rq_too_large: 0
   http.ingress_http.downstream_rq_total: 3
   http.ingress_http.downstream_rq_tx_reset: 0
   http.ingress_http.downstream_rq_ws_on_non_ws_route: 0
   http.ingress_http.no_cluster: 0
   http.ingress_http.no_route: 0
   http.ingress_http.passthrough_internal_redirect_bad_location: 0
   http.ingress_http.passthrough_internal_redirect_no_route: 0
   http.ingress_http.passthrough_internal_redirect_predicate: 0
   http.ingress_http.passthrough_internal_redirect_too_many_redirects: 0
   http.ingress_http.passthrough_internal_redirect_unsafe_scheme: 0
   http.ingress_http.rq_direct_response: 0
   http.ingress_http.rq_redirect: 0
   http.ingress_http.rq_reset_after_downstream_response_started: 0
   http.ingress_http.rq_total: 3
   http.ingress_http.rs_too_large: 0
   http.ingress_http.tracing.client_enabled: 0
   http.ingress_http.tracing.health_check: 0
   http.ingress_http.tracing.not_traceable: 0
   http.ingress_http.tracing.random_sampling: 0
   http.ingress_http.tracing.service_forced: 0
   http.ingress_http.downstream_cx_length_ms: P0(nan,2.0) P25(nan,2.075) P50(nan,3.05) P75(nan,17.25) P90(nan,17.7) P95(nan,17.85) P99(nan,17.97) P99.5(nan,17.985) P99.9(nan,17.997) P100(nan,18.0)
   http.ingress_http.downstream_rq_time: P0(nan,1.0) P25(nan,1.075) P50(nan,2.05) P75(nan,16.25) P90(nan,16.7) P95(nan,16.85) P99(nan,16.97) P99.5(nan,16.985) P99.9(nan,16.997) P100(nan,17.0)


You can also pass a ``format`` argument, for example to return ``json``:

.. code-block:: console

   $ curl -s "http://localhost:19000/stats?filter=http.ingress_http.rq&format=json" | jq '.'
   {
     "stats": [
       {
         "value": 0,
	 "name": "http.ingress_http.rq_direct_response"
       },
       {
         "value": 0,
	 "name": "http.ingress_http.rq_redirect"
       },
       {
         "value": 0,
	 "name": "http.ingress_http.rq_reset_after_downstream_response_started"
       },
       {
         "value": 3,
	 "name": "http.ingress_http.rq_total"
       }
     ]
   }
