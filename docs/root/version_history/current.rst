1.19.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* http: replaced setting `envoy.reloadable_features.strict_1xx_and_204_response_headers` with settings
  `envoy.reloadable_features.require_strict_1xx_and_204_response_headers`
  (require upstream 1xx or 204 responses to not have Transfer-Encoding or non-zero Content-Length headers) and
  `envoy.reloadable_features.send_strict_1xx_and_204_response_headers`
  (do not send 1xx or 204 responses with these headers). Both are true by default.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* validation: fix an issue that causes TAP sockets to panic during config validation mode.
* xray: fix the default sampling 'rate' for AWS X-Ray tracer extension to be 5% as opposed to 50%.
* zipkin: fix timestamp serializaiton in annotations. A prior bug fix exposed an issue with timestamps being serialized as strings.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed `envoy.reloadable_features.allow_500_after_100` runtime guard and the legacy code path.
* http: removed `envoy.reloadable_features.hcm_stream_error_on_invalid_message` for disabling closing HTTP/1.1 connections on error. Connection-closing can still be disabled by setting the HTTP/1 configuration :ref:`override_stream_error_on_invalid_http_message <envoy_v3_api_field_config.core.v3.Http1ProtocolOptions.override_stream_error_on_invalid_http_message>`.
* http: removed `envoy.reloadable_features.overload_manager_disable_keepalive_drain_http2`; Envoy will now always send GOAWAY to HTTP2 downstreams when the :ref:`disable_keepalive <config_overload_manager_overload_actions>` overload action is active.
* http: removed `envoy.reloadable_features.unify_grpc_handling` runtime guard and legacy code paths.
* tls: removed `envoy.reloadable_features.tls_use_io_handle_bio` runtime guard and legacy code path.

New Features
------------

* access log: added a new :ref:`OpenTelemetry access logger <envoy_v3_api_msg_extensions.access_loggers.open_telemetry.v3alpha.OpenTelemetryAccessLogConfig>` extension, allowing a flexible log structure with native Envoy access log formatting.
* access log: added the new response flag `NC` for upstream cluster not found. The error flag is set when the http or tcp route is found for the request but the cluster is not available.
* access log: added the :ref:`formatters <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.formatters>` extension point for custom formatters (command operators).
* access log: added support for cross platform writing to :ref:`standard output <envoy_v3_api_msg_extensions.access_loggers.stream.v3.StdoutAccessLog>` and :ref:`standard error <envoy_v3_api_msg_extensions.access_loggers.stream.v3.StderrAccessLog>`.
* access log: support command operator: %FILTER_CHAIN_NAME% for the downstream tcp and http request.
* access log: support command operator: %REQUEST_HEADERS_BYTES%, %RESPONSE_HEADERS_BYTES%, and %RESPONSE_TRAILERS_BYTES%.
* admin: added support for :ref:`access loggers <envoy_v3_api_msg_config.accesslog.v3.AccessLog>` to the admin interface.
* compression: add brotli :ref:`compressor <envoy_v3_api_msg_extensions.compression.brotli.compressor.v3.Brotli>` and :ref:`decompressor <envoy_v3_api_msg_extensions.compression.brotli.decompressor.v3.Brotli>`.
* compression: extended the compression allow compressing when the content length header is not present. This behavior may be temporarily reverted by setting `envoy.reloadable_features.enable_compression_without_content_length_header` to false.
* config: add `envoy.features.fail_on_any_deprecated_feature` runtime key, which matches the behaviour of compile-time flag `ENVOY_DISABLE_DEPRECATED_FEATURES`, i.e. use of deprecated fields will cause a crash.
* config: the ``Node`` :ref:`dynamic context parameters <envoy_v3_api_field_config.core.v3.Node.dynamic_parameters>` are populated in discovery requests when set on the server instance.
* dispatcher: supports a stack of `Envoy::ScopeTrackedObject` instead of a single tracked object. This will allow Envoy to dump more debug information on crash.
* ext_authz: added :ref:`response_headers_to_add <envoy_v3_api_field_service.auth.v3.OkHttpResponse.response_headers_to_add>` to support sending response headers to downstream clients on OK authorization checks via gRPC.
* ext_authz: added :ref:`allowed_client_headers_on_success <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.AuthorizationResponse.allowed_client_headers_on_success>` to support sending response headers to downstream clients on OK external authorization checks via HTTP.
* grpc_json_transcoder: added :ref:`request_validation_options <envoy_v3_api_field_extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder.request_validation_options>` to reject invalid requests early.
* grpc_json_transcoder: filter can now be configured on per-route/per-vhost level as well. Leaving empty list of services in the filter configuration disables transcoding on the specific route.
* http: added new HTTP/1.1 parser llhttp. This is off by default and can be enabled by setting the `envoy.reloadable_features.enable_new_http1_parser` runtime feature to true.
* http: added support for `Envoy::ScopeTrackedObject` for HTTP/1 and HTTP/2 dispatching. Crashes while inside the dispatching loop should dump debug information. Furthermore, HTTP/1 and HTTP/2 clients now dumps the originating request whose response from the upstream caused Envoy to crash.
* http: added support for :ref:`preconnecting <envoy_v3_api_msg_config.cluster.v3.Cluster.PreconnectPolicy>`. Preconnecting is off by default, but recommended for clusters serving latency-sensitive traffic, especially if using HTTP/1.1.
* http: added support for stream filters to mutate the cached route set by HCM route resolution. Useful for filters in a filter chain that want to override specific methods/properties of a route. See :ref:`http route mutation <arch_overview_http_filters_route_mutation>` docs for more information.
* http: added new runtime config `envoy.reloadable_features.check_unsupported_typed_per_filter_config`, the default value is true. When the value is true, envoy will reject virtual host-specific typed per filter config when the filter doesn't support it.
* http: added the ability to preserve HTTP/1 header case across the proxy. See the :ref:`header casing <config_http_conn_man_header_casing>` documentation for more information.
* http: change frame flood and abuse checks to the upstream HTTP/2 codec to ON by default. It can be disabled by setting the `envoy.reloadable_features.upstream_http2_flood_checks` runtime key to false.
* http: hash multiple header values instead of only hash the first header value. It can be disabled by setting the `envoy.reloadable_features.hash_multiple_header_values` runtime key to false. See the :ref:`HashPolicy's Header configuration <envoy_v3_api_msg_config.route.v3.RouteAction.HashPolicy.Header>` for more information.
* json: introduced new JSON parser (https://github.com/nlohmann/json) to replace RapidJSON. The new parser is disabled by default. To test the new RapidJSON parser, enable the runtime feature `envoy.reloadable_features.remove_legacy_json`.
* kill_request: :ref:`Kill Request <config_http_filters_kill_request>` Now supports bidirection killing.
* loadbalancer: added the ability to specify the hash_key for a host when using a consistent hashing loadbalancer (ringhash, maglev) using the :ref:`LbEndpoint.Metadata <envoy_api_field_endpoint.LbEndpoint.metadata>` e.g.: ``"envoy.lb": {"hash_key": "..."}``.
* log: added a new custom flag ``%j`` to the log pattern to print the actual message to log as JSON escaped string.
* oauth filter: added the optional parameter :ref:`resources <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.resources>`. Set this value to add multiple "resource" parameters in the Authorization request sent to the OAuth provider. This acts as an identifier representing the protected resources the client is requesting a token for.
* original_dst: added support for :ref:`Original Destination <config_listener_filters_original_dst>` on Windows. This enables the use of Envoy as a sidecar proxy on Windows.
* overload: add support for scaling :ref:`transport connection timeouts<envoy_v3_api_enum_value_config.overload.v3.ScaleTimersOverloadActionConfig.TimerType.TRANSPORT_SOCKET_CONNECT>`. This can be used to reduce the TLS handshake timeout in response to overload.
* postgres: added ability to :ref:`terminate SSL<envoy_v3_api_field_extensions.filters.network.postgres_proxy.v3alpha.PostgresProxy.terminate_ssl>`.
* rbac: added :ref:`shadow_rules_stat_prefix <envoy_v3_api_field_extensions.filters.http.rbac.v3.RBAC.shadow_rules_stat_prefix>` to allow adding custom prefix to the stats emitted by shadow rules.
* route config: added :ref:`allow_post field <envoy_v3_api_field_config.route.v3.RouteAction.UpgradeConfig.ConnectConfig.allow_post>` for allowing POST payload as raw TCP.
* route config: added :ref:`max_direct_response_body_size_bytes <envoy_v3_api_field_config.route.v3.RouteConfiguration.max_direct_response_body_size_bytes>` to set maximum :ref:`direct response body <envoy_v3_api_field_config.route.v3.DirectResponseAction.body>` size in bytes. If not specified the default remains 4096 bytes.
* server: added *fips_mode* to :ref:`server compilation settings <server_compilation_settings_statistics>` related statistic.
* server: added :option:`--enable-core-dump` flag to enable core dumps via prctl (Linux-based systems only).
* tcp_proxy: add support for converting raw TCP streams into HTTP/1.1 CONNECT requests. See :ref:`upgrade documentation <tunneling-tcp-over-http>` for details.
* tcp_proxy: added a :ref:`use_post field <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.TunnelingConfig.use_post>` for using HTTP POST to proxy TCP streams.
* tcp_proxy: added a :ref:`headers_to_add field <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.TunnelingConfig.headers_to_add>` for setting additional headers to the HTTP requests for TCP proxing.
* thrift_proxy: added a :ref:`max_requests_per_connection field <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.ThriftProxy.max_requests_per_connection>` for setting maximum requests for per downstream connection.
* thrift_proxy: added per upstream metrics within the :ref:`thrift router <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.router.v3.Router>` for messagetype in request/response.
* tls peer certificate validation: added :ref:`SPIFFE validator <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig>` for supporting isolated multiple trust bundles in a single listener or cluster.
* tracing: added the :ref:`pack_trace_reason <envoy_v3_api_field_extensions.request_id.uuid.v3.UuidRequestIdConfig.pack_trace_reason>`
  field as well as explicit configuration for the built-in :ref:`UuidRequestIdConfig <envoy_v3_api_msg_extensions.request_id.uuid.v3.UuidRequestIdConfig>`
  request ID implementation. See the trace context propagation :ref:`architecture overview
  <arch_overview_tracing_context_propagation>` for more information.
* udp: added :ref:`downstream <config_listener_stats_udp>` and
  :ref:`upstream <config_udp_listener_filters_udp_proxy_stats>` statistics for dropped datagrams.
* udp: added :ref:`downstream_socket_config <envoy_v3_api_field_config.listener.v3.UdpListenerConfig.downstream_socket_config>`
  listener configuration to allow configuration of downstream max UDP datagram size. Also added
  :ref:`upstream_socket_config <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.upstream_socket_config>`
  UDP proxy configuration to allow configuration of upstream max UDP datagram size. The defaults for
  both remain 1500 bytes.
* udp: added configuration for :ref:`GRO
  <envoy_v3_api_field_config.core.v3.UdpSocketConfig.prefer_gro>`. The default is disabled for
  :ref:`downstream sockets <envoy_v3_api_field_config.listener.v3.UdpListenerConfig.downstream_socket_config>`
  and enabled for :ref:`upstream sockets <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.upstream_socket_config>`.

Deprecated
----------
