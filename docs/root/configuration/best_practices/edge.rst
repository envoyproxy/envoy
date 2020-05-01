.. _best_practices_edge:

Configuring Envoy as an edge proxy
==================================

Envoy is a production-ready edge proxy, however, the default settings are tailored
for the service mesh use case, and some values need to be adjusted when using Envoy
as an edge proxy.

TCP proxies should configure:

* restrict access to the admin endpoint,
* :ref:`overload_manager <config_overload_manager>`,
* :ref:`listener buffer limits <envoy_api_field_Listener.per_connection_buffer_limit_bytes>` to 32 KiB,
* :ref:`cluster buffer limits <envoy_api_field_Cluster.per_connection_buffer_limit_bytes>` to 32 KiB.

HTTP proxies should additionally configure:

* :ref:`use_remote_address <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.use_remote_address>`
  to true (to avoid consuming HTTP headers from external clients, see :ref:`HTTP header sanitizing <config_http_conn_man_header_sanitizing>`
  for details),
* :ref:`connection and stream timeouts <faq_configuration_timeouts>`,
* :ref:`HTTP/2 maximum concurrent streams limit <envoy_api_field_core.Http2ProtocolOptions.max_concurrent_streams>` to 100,
* :ref:`HTTP/2 initial stream window size limit <envoy_api_field_core.Http2ProtocolOptions.initial_stream_window_size>` to 64 KiB,
* :ref:`HTTP/2 initial connection window size limit <envoy_api_field_core.Http2ProtocolOptions.initial_connection_window_size>` to 1 MiB.
* :ref:`headers_with_underscores_action setting <envoy_api_field_core.HttpProtocolOptions.headers_with_underscores_action>` to REJECT_REQUEST, to protect upstream services that treat '_' and '-' as interchangeable.

The following is a YAML example of the above recommendation (taken from the :ref:`bug bounty
<arch_overview_bug_bounty>` edge server configuration):

.. literalinclude:: envoy-edge.yaml
  :language: yaml
