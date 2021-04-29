.. _start_quick_start_admin:

Envoy admin interface
=====================

The optional admin interface provided by Envoy allows you to view configuration and statistics, change the
behaviour of the server, and tap traffic according to specific filter rules.

.. note::

   This guide provides configuration information, and some basic examples of using a couple of the admin
   endpoints.

   See the :ref:`admin docs <operations_admin_interface>` for information on all of the available endpoints.

.. admonition:: Requirements

   Some of the examples below make use of the `jq <https://stedolan.github.io/jq/>`_ tool to parse the output
   from the admin server.

.. _start_quick_start_admin_config:

``admin``
---------

The :ref:`admin message <envoy_v3_api_msg_config.bootstrap.v3.Admin>` is required to enable and configure
the administration server.

The ``address`` key specifies the listening :ref:`address <envoy_v3_api_file_envoy/config/core/v3/address.proto>`
which in the demo configuration is ``0.0.0.0:9901``.

In this example, the logs are simply discarded.

.. code-block:: yaml
   :emphasize-lines: 4-5

   admin:
     address:
       socket_address:
         address: 0.0.0.0
         port_value: 9901

.. warning::

   The Envoy admin endpoint can expose private information about the running service, allows modification
   of runtime settings and can also be used to shut the server down.

   As the endpoint is not authenticated it is essential that you limit access to it.

   You may wish to restrict the network address the admin server listens to in your own deployment as part
   of your strategy to limit access to this endpoint.


``stat_prefix``
---------------

The Envoy
:ref:`HttpConnectionManager <envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager>`
must be configured with
:ref:`stat_prefix <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`.

This provides a key that can be filtered when querying the stats interface
:ref:`as shown below <start_quick_start_admin_stats>`

In the :download:`envoy-demo.yaml <_include/envoy-demo.yaml>` the listener is configured with the
:ref:`stat_prefix <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
of ``ingress_http``.

.. literalinclude:: _include/envoy-demo.yaml
    :language: yaml
    :linenos:
    :lines: 1-29
    :emphasize-lines: 13-14

.. _start_quick_start_admin_config_dump:

Admin endpoints: ``config_dump``
--------------------------------

The :ref:`config_dump <operations_admin_interface_config_dump>` endpoint returns Envoy's runtime
configuration in ``json`` format.

The following command allows you to see the types of configuration available:

.. code-block:: console

   $ curl -s http://localhost:9901/config_dump | jq -r '.configs[] | .["@type"]'
   type.googleapis.com/envoy.admin.v3.BootstrapConfigDump
   type.googleapis.com/envoy.admin.v3.ClustersConfigDump
   type.googleapis.com/envoy.admin.v3.ListenersConfigDump
   type.googleapis.com/envoy.admin.v3.ScopedRoutesConfigDump
   type.googleapis.com/envoy.admin.v3.RoutesConfigDump
   type.googleapis.com/envoy.admin.v3.SecretsConfigDump

To view the :ref:`socket_address <envoy_v3_api_msg_config.core.v3.SocketAddress>` of the first
:ref:`dynamic_listener <envoy_v3_api_field_admin.v3.ListenersConfigDump.dynamic_listeners>` currently configured,
you could:

.. code-block:: console

   $ curl -s http://localhost:9901/config_dump?resource=dynamic_listeners | jq '.configs[0].active_state.listener.address'
   {
     "socket_address": {
       "address": "0.0.0.0",
       "port_value": 10000
     }
   }

.. note::

   See the reference section for :ref:`config_dump <operations_admin_interface_config_dump>` for further information
   on available parameters and responses.

.. tip::

   Enabling the :ref:`admin <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.admin>` interface with
   dynamic configuration can be particularly useful as it allows you to use the
   :ref:`config_dump <start_quick_start_admin_config_dump>` endpoint to see how Envoy is configured at
   a particular point in time.

.. _start_quick_start_admin_stats:

Admin endpoints: ``stats``
--------------------------

The :ref:`admin stats <operations_stats>` endpoint allows you to retrieve runtime information about Envoy.

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

The stats endpoint accepts a :ref:`filter <operations_admin_interface_stats>` argument, which
is evaluated as a regular expression:

.. code-block:: console

   $ curl -s http://localhost:9901/stats?filter='^http\.ingress_http'
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


You can also pass a :ref:`format <operations_admin_interface_stats>` argument, for example to return ``json``:

.. code-block:: console

   $ curl -s "http://localhost:9901/stats?filter=http.ingress_http.rq&format=json" | jq '.stats'

.. code-block:: json

   [
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


Envoy admin web UI
------------------

Envoy also has a web user interface that allows you to view and modify settings and
statistics.

Point your browser to http://localhost:9901.

.. image:: /_static/envoy-admin.png
