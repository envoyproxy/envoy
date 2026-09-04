.. _best_practices_edge:

Configuring Envoy as an edge proxy
==================================

Envoy is a production-ready edge proxy, however, the default settings are tailored
for the service mesh use case, and some values need to be adjusted when using Envoy
as an edge proxy.

TCP proxies should configure:

* restrict access to the admin endpoint,
* :ref:`overload_manager <config_overload_manager>`,
* :ref:`listener buffer limits <envoy_v3_api_field_config.listener.v3.Listener.per_connection_buffer_limit_bytes>` to 32 KiB,
* :ref:`cluster buffer limits <envoy_v3_api_field_config.cluster.v3.Cluster.per_connection_buffer_limit_bytes>` to 32 KiB.

HTTP proxies should additionally configure:

* :ref:`use_remote_address <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.use_remote_address>`
  to true (to avoid consuming HTTP headers from external clients, see :ref:`HTTP header sanitizing <config_http_conn_man_header_sanitizing>`
  for details),
* :ref:`connection and stream timeouts <faq_configuration_timeouts>`,
* :ref:`HTTP/2 maximum concurrent streams limit <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_concurrent_streams>` and :ref:`HTTP/3 maximum concurrent streams limit <envoy_v3_api_field_config.core.v3.QuicProtocolOptions.max_concurrent_streams>` to 100
* :ref:`HTTP/2 initial stream window size limit <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.initial_stream_window_size>` to 64 KiB,
* :ref:`HTTP/2 initial connection window size limit <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.initial_connection_window_size>` to 1 MiB.
* :ref:`headers_with_underscores_action setting <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.headers_with_underscores_action>` to REJECT_REQUEST, to protect upstream services that treat '_' and '-' as interchangeable.
* :ref:`Listener connection limits. <config_listeners_runtime>`
* :ref:`Global downstream connection limits <config_overload_manager>`.

If Envoy is configured with RBAC filter or makes route selection based on URL path it is recommended to enable the following path normalization options to minimize probability of path confusion vulnerabilities. Path confusion vulnerabilities occur when parties participating in request use different path representations.

* Enable :ref:`normalize_path setting <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.normalize_path>`.
* Enable :ref:`merge_slashes setting <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.merge_slashes>`.

Additionally the :ref:`path_with_escaped_slashes_action setting <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.path_with_escaped_slashes_action>` should be set according to following recommendations:

* REJECT_REQUEST if dowstream clients are expected to use `RFC 3986 <https://www.ietf.org/rfc/rfc3986.txt>`_ compliant normalized paths (i.e. gRPC clients).
* UNESCAPE_AND_REDIRECT if downstream client supports HTTP redirect (i.e. a browser). This option minimizes possibility of path confusion by forcing request to be re-issued with the same path across all parties: downstream client, Envoy and upstream server. Note that gRPC requests will still be rejected with the INTERNAL (13) error code, as gRPC clients do not support redirect.
* KEEP_UNCHANGED for servers that are not `RFC 3986 <https://www.ietf.org/rfc/rfc3986.txt>`_ compliant and require encoded slashes.
* UNESCAPE_AND_FORWARD for servers that are known to treat escaped and unescaped slashes equivalently. Choosing this option may increase probablity of path confusion vulnerabilities if intermediaries perform path based access control.

The following is a YAML example of the above recommendation (taken from the :ref:`Google VRP
<arch_overview_google_vrp>` edge server configuration):

.. literalinclude:: _include/edge.yaml
    :language: yaml
