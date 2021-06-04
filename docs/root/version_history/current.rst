1.19.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* grpc_bridge_filter: the filter no longer collects grpc stats in favor of the existing grpc stats filter.
  The behavior can be reverted by changing runtime key ``envoy.reloadable_features.grpc_bridge_stats_disabled``.
* tracing: update Apache SkyWalking tracer version to be compatible with 8.4.0 data collect protocol. This change will introduce incompatibility with SkyWalking 8.3.0.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* access_log: add new access_log command operator ``%REQUEST_TX_DURATION%``.
* aws_request_signing: requests are now buffered by default to compute signatures which include the
  payload hash, making the filter compatible with most AWS services. Previously, requests were
  never buffered, which only produced correct signatures for requests without a body, or for
  requests to S3, ES or Glacier, which used the literal string ``UNSIGNED-PAYLOAD``. Buffering can
  be now be disabled in favor of using unsigned payloads with compatible services via the new
  `use_unsigned_payload` filter option (default false).
* cluster: added default value of 5 seconds for :ref:`connect_timeout <envoy_v3_api_field_config.cluster.v3.Cluster.connect_timeout>`.
* http: disable the integration between :ref:`ExtensionWithMatcher <envoy_v3_api_msg_extensions.common.matching.v3.ExtensionWithMatcher>`
  and HTTP filters by default to reflects its experimental status. This feature can be enabled by seting
  ``envoy.reloadable_features.experimental_matching_api`` to true.
* http: replaced setting ``envoy.reloadable_features.strict_1xx_and_204_response_headers`` with settings
  ``envoy.reloadable_features.require_strict_1xx_and_204_response_headers``
  (require upstream 1xx or 204 responses to not have Transfer-Encoding or non-zero Content-Length headers) and
  ``envoy.reloadable_features.send_strict_1xx_and_204_response_headers``
  (do not send 1xx or 204 responses with these headers). Both are true by default.
* http: serve HEAD requests from cache.
* http: the behavior of the *present_match* in route header matcher changed. The value of *present_match* is ignored in the past. The new behavior is *present_match* performed when value is true. absent match performed when the value is false. Please reference :ref:`present_match
  <envoy_v3_api_field_config.route.v3.HeaderMatcher.present_match>`.
* listener: respect the :ref:`connection balance config <envoy_v3_api_field_config.listener.v3.Listener.connection_balance_config>`
  defined within the listener where the sockets are redirected to. Clear that field to restore the previous behavior.
* tcp: switched to the new connection pool by default. Any unexpected behavioral changes can be reverted by setting runtime guard ``envoy.reloadable_features.new_tcp_connection_pool`` to false.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* aws_lambda: if `payload_passthrough` is set to ``false``, the downstream response content-type header will now be set from the content-type entry in the JSON response's headers map, if present.
* hot_restart: fix double counting of `server.seconds_until_first_ocsp_response_expiring` and `server.days_until_first_cert_expiring` during hot-restart. This stat was only incorrect until the parent process terminated.
* http: port stripping now works for CONNECT requests, though the port will be restored if the CONNECT request is sent upstream. This behavior can be temporarily reverted by setting ``envoy.reloadable_features.strip_port_from_connect`` to false.
* http: raise max configurable max_request_headers_kb limit to 8192 KiB (8MiB) from 96 KiB in http connection manager.
* listener: fix the crash which could happen when the ongoing filter chain only listener update is followed by the listener removal or full listener update.
* udp: limit each UDP listener to read maxmium 6000 packets per event loop. This behavior can be temporarily reverted by setting ``envoy.reloadable_features.udp_per_event_loop_read_limit`` to false.
* validation: fix an issue that causes TAP sockets to panic during config validation mode.
* xray: fix the default sampling 'rate' for AWS X-Ray tracer extension to be 5% as opposed to 50%.
* zipkin: fix timestamp serializaiton in annotations. A prior bug fix exposed an issue with timestamps being serialized as strings.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* event: removed ``envoy.reloadable_features.activate_timers_next_event_loop`` runtime guard and legacy code path.
* http: removed ``envoy.reloadable_features.allow_500_after_100`` runtime guard and the legacy code path.
* http: removed ``envoy.reloadable_features.always_apply_route_header_rules`` runtime guard and legacy code path.
* http: removed ``envoy.reloadable_features.hcm_stream_error_on_invalid_message`` for disabling closing HTTP/1.1 connections on error. Connection-closing can still be disabled by setting the HTTP/1 configuration :ref:`override_stream_error_on_invalid_http_message <envoy_v3_api_field_config.core.v3.Http1ProtocolOptions.override_stream_error_on_invalid_http_message>`.
* http: removed ``envoy.reloadable_features.http_set_copy_replace_all_headers`` runtime guard and legacy code paths.
* http: removed ``envoy.reloadable_features.overload_manager_disable_keepalive_drain_http2``; Envoy will now always send GOAWAY to HTTP2 downstreams when the :ref:`disable_keepalive <config_overload_manager_overload_actions>` overload action is active.
* http: removed ``envoy.reloadable_features.http_match_on_all_headers`` runtime guard and legacy code paths.
* http: removed ``envoy.reloadable_features.unify_grpc_handling`` runtime guard and legacy code paths.
* tls: removed ``envoy.reloadable_features.tls_use_io_handle_bio`` runtime guard and legacy code path.

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
* http: added support for `Envoy::ScopeTrackedObject` for HTTP/1 dispatching. Crashes while inside the dispatching loop should dump debug information.
* http: added support for :ref:`max_requests_per_connection <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_requests_per_connection>` for downstream connection.
* http: added support for `Envoy::ScopeTrackedObject` for HTTP/1 and HTTP/2 dispatching. Crashes while inside the dispatching loop should dump debug information.
* http: added an option to not add content-length: 0 for requests which should not have bodies. This behavior can be enabled by setting `envoy.reloadable_features.dont_add_content_length_for_bodiless_requests` true.
* http: added support for `Envoy::ScopeTrackedObject` for HTTP/1 and HTTP/2 dispatching. Crashes while inside the dispatching loop should dump debug information. Furthermore, HTTP/1 and HTTP/2 clients now dumps the originating request whose response from the upstream caused Envoy to crash.
* http: added support for :ref:`preconnecting <envoy_v3_api_msg_config.cluster.v3.Cluster.PreconnectPolicy>`. Preconnecting is off by default, but recommended for clusters serving latency-sensitive traffic, especially if using HTTP/1.1.
* http: added new runtime config `envoy.reloadable_features.check_unsupported_typed_per_filter_config`, the default value is true. When the value is true, envoy will reject virtual host-specific typed per filter config when the filter doesn't support it.
* http: added the ability to preserve HTTP/1 header case across the proxy. See the :ref:`header casing <config_http_conn_man_header_casing>` documentation for more information.
* http: change frame flood and abuse checks to the upstream HTTP/2 codec to ON by default. It can be disabled by setting the `envoy.reloadable_features.upstream_http2_flood_checks` runtime key to false.
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
* bandwidth_limit: added new :ref:`HTTP bandwidth limit filter <config_http_filters_bandwidth_limit>`.
* bootstrap: added :ref:`dns_resolution_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dns_resolution_config>` to aggregate all of the DNS resolver configuration in a single message. By setting one such configuration option *no_default_search_domain* as true the DNS resolver will not use the default search domains. And by setting the configuration *resolvers* we can specify the external DNS servers to be used for external DNS query.
* cluster: added :ref:`dns_resolution_config <envoy_v3_api_field_config.cluster.v3.Cluster.dns_resolution_config>` to aggregate all of the DNS resolver configuration in a single message. By setting one such configuration option *no_default_search_domain* as true the DNS resolver will not use the default search domains.
* crash support: restore crash context when continuing to processing requests or responses as a result of an asynchronous callback that invokes a filter directly. This is unlike the call stacks that go through the various network layers, to eventually reach the filter. For a concrete example see: ``Envoy::Extensions::HttpFilters::Cache::CacheFilter::getHeaders`` which posts a callback on the dispatcher that will invoke the filter directly.
* dns resolver: added *DnsResolverOptions* protobuf message to reconcile all of the DNS lookup option flags. By setting the configuration option :ref:`use_tcp_for_dns_lookups <envoy_v3_api_field_config.core.v3.DnsResolverOptions.use_tcp_for_dns_lookups>` as true we can make the underlying dns resolver library to make only TCP queries to the DNS servers and by setting the configuration option :ref:`no_default_search_domain <envoy_v3_api_field_config.core.v3.DnsResolverOptions.no_default_search_domain>` as true the DNS resolver library will not use the default search domains.
* dns resolver: added *DnsResolutionConfig* to combine :ref:`dns_resolver_options <envoy_v3_api_field_config.core.v3.DnsResolutionConfig.dns_resolver_options>` and :ref:`resolvers <envoy_v3_api_field_config.core.v3.DnsResolutionConfig.resolvers>` in a single protobuf message. The field *resolvers* can be specified with a list of DNS resolver addresses. If specified, DNS client library will perform resolution via the underlying DNS resolvers. Otherwise, the default system resolvers (e.g., /etc/resolv.conf) will be used.
* dns_filter: added :ref:`dns_resolution_config <envoy_v3_api_field_extensions.filters.udp.dns_filter.v3alpha.DnsFilterConfig.ClientContextConfig.dns_resolution_config>` to aggregate all of the DNS resolver configuration in a single message. By setting the configuration option *use_tcp_for_dns_lookups* to true we can make dns filter's external resolvers to answer queries using TCP only, by setting the configuration option *no_default_search_domain* as true the DNS resolver will not use the default search domains. And by setting the configuration *resolvers* we can specify the external DNS servers to be used for external DNS query which replaces the pre-existing alpha api field *upstream_resolvers*.
* dynamic_forward_proxy: added :ref:`dns_resolution_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_resolution_config>` option to the DNS cache config in order to aggregate all of the DNS resolver configuration in a single message. By setting one such configuration option *no_default_search_domain* as true the DNS resolver will not use the default search domains. And by setting the configuration *resolvers* we can specify the external DNS servers to be used for external DNS query instead of the system default resolvers.
* http: a new field `is_optional` is added to `extensions.filters.network.http_connection_manager.v3.HttpFilter`. When
  value is `true`, the unsupported http filter will be ignored by envoy. This is also same with unsupported http filter
  in the typed per filter config. For more information, please reference
  :ref:`HttpFilter <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.is_optional>`.
* http: added :ref:`stripping trailing host dot from host header<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.strip_trailing_host_dot>` support.
* http: added support for :ref:`original IP detection extensions<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.original_ip_detection_extensions>`.
  Two initial extensions were added, the :ref:`custom header <envoy_v3_api_msg_extensions.http.original_ip_detection.custom_header.v3.CustomHeaderConfig>` extension and the
  :ref:`xff <envoy_v3_api_msg_extensions.http.original_ip_detection.xff.v3.XffConfig>` extension.
* http: added the ability to :ref:`unescape slash sequences<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.path_with_escaped_slashes_action>` in the path. Requests with unescaped slashes can be proxied, rejected or redirected to the new unescaped path. By default this feature is disabled. The default behavior can be overridden through :ref:`http_connection_manager.path_with_escaped_slashes_action<config_http_conn_man_runtime_path_with_escaped_slashes_action>` runtime variable. This action can be selectively enabled for a portion of requests by setting the :ref:`http_connection_manager.path_with_escaped_slashes_action_sampling<config_http_conn_man_runtime_path_with_escaped_slashes_action_enabled>` runtime variable.
* http: added upstream and downstream alpha HTTP/3 support! See :ref:`quic_options <envoy_v3_api_field_config.listener.v3.UdpListenerConfig.quic_options>` for downstream and the new http3_protocol_options in :ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>` for upstream HTTP/3.
* jwt_authn: added support to fetch remote jwks asynchronously specified by :ref:`async_fetch <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.RemoteJwks.async_fetch>`.
* listener: added ability to change an existing listener's address.
* local_rate_limit_filter: added suppoort for locally rate limiting http requests on a per connection basis. This can be enabled by setting the :ref:`local_rate_limit_per_downstream_connection <envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.local_rate_limit_per_downstream_connection>` field to true.
* metric service: added support for sending metric tags as labels. This can be enabled by setting the :ref:`emit_tags_as_labels <envoy_v3_api_field_config.metrics.v3.MetricsServiceConfig.emit_tags_as_labels>` field to true.
* proxy protocol: added support for generating the header while using the :ref:`HTTP connection manager <config_http_conn_man>`. This is done using the using the :ref:`Proxy Protocol Transport Socket <extension_envoy.transport_sockets.upstream_proxy_protocol>` on upstream clusters.
  This feature is currently affected by a memory leak `issue <https://github.com/envoyproxy/envoy/issues/16682>`_.
* tcp: added support for :ref:`preconnecting <v1.18.0:envoy_v3_api_msg_config.cluster.v3.Cluster.PreconnectPolicy>`. Preconnecting is off by default, but recommended for clusters serving latency-sensitive traffic.
* thrift_proxy: added per upstream metrics within the :ref:`thrift router <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.router.v3.Router>` for request and response size histograms.
* tls: allow dual ECDSA/RSA certs via SDS. Previously, SDS only supported a single certificate per context, and dual cert was only supported via non-SDS.
* udp_proxy: added :ref:`key <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.HashPolicy>` as another hash policy to support hash based routing on any given key.

Deprecated
----------

* bootstrap: the field :ref:`use_tcp_for_dns_lookups <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.use_tcp_for_dns_lookups>` is deprecated in favor of :ref:`dns_resolution_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dns_resolution_config>` which aggregates all of the DNS resolver configuration in a single message.
* cluster: the fields :ref:`use_tcp_for_dns_lookups <envoy_v3_api_field_config.cluster.v3.Cluster.use_tcp_for_dns_lookups>` and :ref:`dns_resolvers <envoy_v3_api_field_config.cluster.v3.Cluster.dns_resolvers>` are deprecated in favor of :ref:`dns_resolution_config <envoy_v3_api_field_config.cluster.v3.Cluster.dns_resolution_config>` which aggregates all of the DNS resolver configuration in a single message.
* dynamic_forward_proxy: the field :ref:`use_tcp_for_dns_lookups <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.use_tcp_for_dns_lookups>` is deprecated in favor of :ref:`dns_resolution_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_resolution_config>` which aggregates all of the DNS resolver configuration in a single message.
* http: :ref:`xff_num_trusted_hops <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.xff_num_trusted_hops>` is deprecated in favor of :ref:`original IP detection extensions<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.original_ip_detection_extensions>`.
