1.18.0 (April 15, 2021)
=======================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* config: the v2 xDS API is no longer supported by the Envoy binary.
* grpc_stats: the default value for :ref:`stats_for_all_methods <envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.stats_for_all_methods>` is switched from true to false, in order to avoid possible memory exhaustion due to an untrusted downstream sending a large number of unique method names. The previous default value was deprecated in version 1.14.0. This only changes the behavior when the value is not set. The previous behavior can be used by setting the value to true. This behavior change by be overridden by setting runtime feature `envoy.deprecated_features.grpc_stats_filter_enable_stats_for_all_methods_by_default`.
* http: fixing a standards compliance issue with :scheme. The :scheme header sent upstream is now based on the original URL scheme, rather than set based on the security of the upstream connection. This behavior can be temporarily reverted by setting `envoy.reloadable_features.preserve_downstream_scheme` to false.
* http: http3 is now enabled/disabled via build option `--define http3=disabled` rather than the extension framework. The behavior is the same, but builds may be affected for platforms or build configurations where http3 is not supported.
* http: resolving inconsistencies between :scheme and X-Forwarded-Proto. :scheme will now be set for all HTTP/1.1 requests. This changes the behavior of the gRPC access logger, Wasm filters, CSRF filter and oath2 filter for HTTP/1 traffic, where :scheme was previously not set. This change also validates that for front-line Envoys (Envoys configured with  :ref:`xff_num_trusted_hops <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.xff_num_trusted_hops>` set to 0 and :ref:`use_remote_address <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.use_remote_address>` set to true) that HTTP/1.1 https schemed requests can not be sent over non-TLS connections. All behavioral changes listed here can be temporarily reverted by setting `envoy.reloadable_features.add_and_validate_scheme_header` to false.
* http: when a protocol error is detected in response from upstream, Envoy sends 502 BadGateway downstream and access log entry contains UPE flag. This behavior change can be overwritten to use error code 503 by setting `envoy.reloadable_features.return_502_for_upstream_protocol_errors` to false.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* access_logs: change command operator %UPSTREAM_CLUSTER% to resolve to :ref:`alt_stat_name <envoy_v3_api_field_config.cluster.v3.Cluster.alt_stat_name>` if provided. This behavior can be reverted by disabling the runtime feature `envoy.reloadable_features.use_observable_cluster_name`.
* access_logs: fix substition formatter to recognize commands ending with an integer such as DOWNSTREAM_PEER_FINGERPRINT_256.
* access_logs: set the error flag `NC` for `no cluster found` instead of `NR` if the route is found but the corresponding cluster is not available.
* admin: added :ref:`observability_name <envoy_v3_api_field_admin.v3.ClusterStatus.observability_name>` information to GET /clusters?format=json :ref:`cluster status <envoy_v3_api_msg_admin.v3.ClusterStatus>`.
* dns: both the :ref:`strict DNS <arch_overview_service_discovery_types_strict_dns>` and
  :ref:`logical DNS <arch_overview_service_discovery_types_logical_dns>` cluster types now honor the
  :ref:`hostname <envoy_v3_api_field_config.endpoint.v3.Endpoint.hostname>` field if not empty.
  Previously resolved hosts would have their hostname set to the configured DNS address for use with
  logging, :ref:`auto_host_rewrite <envoy_api_field_route.RouteAction.auto_host_rewrite>`, etc.
  Setting the hostname manually allows overriding the internal hostname used for such features while
  still allowing the original DNS resolution name to be used.
* grpc_json_transcoder: the filter now adheres to encoder and decoder buffer limits. Requests and responses
  that require buffering over the limits will be directly rejected. The behavior can be reverted by
  disabling runtime feature `envoy.reloadable_features.grpc_json_transcoder_adhere_to_buffer_limits`.
  To reduce or increase the buffer limits the filter adheres to, reference the :ref:`flow control documentation <faq_flow_control>`.
* hds: support custom health check port via :ref:`health_check_config <envoy_v3_api_msg_config.endpoint.v3.endpoint.healthcheckconfig>`.
* healthcheck: the :ref:`health check filter <config_http_filters_health_check>` now sends the
  :ref:`x-envoy-immediate-health-check-fail <config_http_filters_router_x-envoy-immediate-health-check-fail>` header
  for all responses when Envoy is in the health check failed state. Additionally, receiving the
  :ref:`x-envoy-immediate-health-check-fail <config_http_filters_router_x-envoy-immediate-health-check-fail>`
  header (either in response to normal traffic or in response to an HTTP :ref:`active health check <arch_overview_health_checking>`) will
  cause Envoy to immediately :ref:`exclude <arch_overview_load_balancing_excluded>` the host from
  load balancing calculations. This has the useful property that such hosts, which are being
  explicitly told to disable traffic, will not be counted for panic routing calculations. See the
  excluded documentation for more information. This behavior can be temporarily reverted by setting
  the `envoy.reloadable_features.health_check.immediate_failure_exclude_from_cluster` feature flag
  to false. Note that the runtime flag covers *both* the health check filter responding with
  `x-envoy-immediate-health-check-fail` in all cases (versus just non-HC requests) as well as
  whether receiving `x-envoy-immediate-health-check-fail` will cause exclusion or not. Thus,
  depending on the Envoy deployment, the feature flag may need to be flipped on both downstream
  and upstream instances, depending on the reason.
* http: added support for internal redirects with bodies. This behavior can be disabled temporarily by setting `envoy.reloadable_features.internal_redirects_with_body` to false.
* http: increase the maximum allowed number of initial connection WINDOW_UPDATE frames sent by the peer from 1 to 5.
* http: no longer adding content-length: 0 for requests which should not have bodies. This behavior can be temporarily reverted by setting `envoy.reloadable_features.dont_add_content_length_for_bodiless_requests` false.
* http: switched the path canonicalizer to `googleurl <https://quiche.googlesource.com/googleurl>`_
  instead of `//source/common/chromium_url`. The new path canonicalizer is enabled by default. To
  revert to the legacy path canonicalizer, enable the runtime flag
  `envoy.reloadable_features.remove_forked_chromium_url`.
* http: upstream flood and abuse checks now increment the count of opened HTTP/2 streams when Envoy sends
  initial HEADERS frame for the new stream. Before the counter was incrementred when Envoy received
  response HEADERS frame with the END_HEADERS flag set from upstream server.
* lua: added function `timestamp` to provide millisecond resolution timestamps by passing in `EnvoyTimestampResolution.MILLISECOND`.
* oauth filter: added the optional parameter :ref:`auth_scopes <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.auth_scopes>` with default value of 'user' if not provided. This allows this value to be overridden in the Authorization request to the OAuth provider.
* perf: allow reading more bytes per operation from raw sockets to improve performance.
* router: extended custom date formatting to DOWNSTREAM_PEER_CERT_V_START and DOWNSTREAM_PEER_CERT_V_END when using :ref:`custom request/response header formats <config_http_conn_man_headers_custom_request_headers>`.
* router: made the path rewrite available without finalizing headers, so the filter could calculate the current value of the final url.
* tracing: added `upstream_cluster.name` tag that resolves to resolve to :ref:`alt_stat_name <envoy_v3_api_field_config.cluster.v3.Cluster.alt_stat_name>` if provided (and otherwise the cluster name).
* udp: configuration has been added for :ref:`GRO <envoy_v3_api_field_config.core.v3.UdpSocketConfig.prefer_gro>`
  which used to be force enabled if the OS supports it. The default is now disabled for server
  sockets and enabled for client sockets (see the new features section for links).
* upstream: host weight changes now cause a full load balancer rebuild as opposed to happening
  atomically inline. This change has been made to support load balancer pre-computation of data
  structures based on host weight, but may have performance implications if host weight changes
  are very frequent. This change can be disabled by setting the `envoy.reloadable_features.upstream_host_weight_change_causes_rebuild`
  feature flag to false. If setting this flag to false is required in a deployment please open an
  issue against the project.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* active http health checks: properly handles HTTP/2 GOAWAY frames from the upstream. Previously a GOAWAY frame due to a graceful listener drain could cause improper failed health checks due to streams being refused by the upstream on a connection that is going away. To revert to old GOAWAY handling behavior, set the runtime feature `envoy.reloadable_features.health_check.graceful_goaway_handling` to false.
* adaptive concurrency: fixed a bug where concurrent requests on different worker threads could update minRTT back-to-back.
* buffer: tighten network connection read and write buffer high watermarks in preparation to more careful enforcement of read limits. Buffer high-watermark is now set to the exact configured value; previously it was set to value + 1.
* cdn_loop: check that the entirety of the :ref:`cdn_id <envoy_v3_api_field_extensions.filters.http.cdn_loop.v3alpha.CdnLoopConfig.cdn_id>` field is a valid CDN identifier.
* cds: fix blocking the update for a warming cluster when the update is the same as the active version.
* ext_authz: emit :ref:`CheckResponse.dynamic_metadata <envoy_v3_api_field_service.auth.v3.CheckResponse.dynamic_metadata>` when the external authorization response has "Denied" check status.
* fault injection: stop counting as active fault after delay elapsed. Previously fault injection filter continues to count the injected delay as an active fault even after it has elapsed. This produces incorrect output statistics and impacts the max number of consecutive faults allowed (e.g., for long-lived streams). This change decreases the active fault count when the delay fault is the only active and has gone finished.
* filter_chain: fix filter chain matching with the server name as the case-insensitive way.
* grpc-web: fix local reply and non-proto-encoded gRPC response handling for small response bodies. This fix can be temporarily reverted by setting `envoy.reloadable_features.grpc_web_fix_non_proto_encoded_response_handling` to false.
* grpc_http_bridge: the downstream HTTP status is now correctly set for trailers-only responses from the upstream.
* header map: pick the right delimiter to append multiple header values to the same key. Previouly header with multiple values were coalesced with ",", after this fix cookie headers should be coalesced with " ;". This doesn't affect Http1 or Http2 requests because these 2 codecs coalesce cookie headers before adding it to header map. To revert to the old behavior, set the runtime feature `envoy.reloadable_features.header_map_correctly_coalesce_cookies` to false.
* http: avoid grpc-status overwrite on when sending local replies if that field has already been set.
* http: disallowing "host:" in request_headers_to_add for behavioral consistency with rejecting :authority header. This behavior can be temporarily reverted by setting `envoy.reloadable_features.treat_host_like_authority` to false.
* http: fixed an issue where Enovy did not handle peer stream limits correctly, and queued streams in nghttp2 rather than establish new connections. This behavior can be temporarily reverted by setting `envoy.reloadable_features.improved_stream_limit_handling` to false.
* http: fixed a bug where setting :ref:`MaxStreamDuration proto <envoy_v3_api_msg_config.route.v3.RouteAction.MaxStreamDuration>` did not disable legacy timeout defaults.
* http: fixed a crash upon receiving empty HTTP/2 metadata frames. Received empty metadata frames are now counted in the HTTP/2 codec stat :ref:`metadata_empty_frames <config_http_conn_man_stats_per_codec>`.
* http: fixed a remotely exploitable integer overflow via a very large grpc-timeout value causes undefined behavior.
* http: reverting a behavioral change where upstream connect timeouts were temporarily treated differently from other connection failures. The change back to the original behavior can be temporarily reverted by setting `envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure` to false.
* jwt_authn: reject requests with a proper error if JWT has the wrong issuer when allow_missing is used. Before this change, the requests are accepted.
* listener: prevent crashing when an unknown listener config proto is received and debug logging is enabled.
* mysql_filter: improve the codec ability of mysql filter at connection phase, it can now decode MySQL5.7+ connection phase protocol packet.
* overload: fix a bug that can cause use-after-free when one scaled timer disables another one with the same duration.
* sni: as the server name in sni should be case-insensitive, envoy will convert the server name as lower case first before any other process inside envoy.
* tls: fix a crash when peer sends a TLS Alert with an unknown code.
* tls: fix the subject alternative name of the presented certificate matches the specified matchers as the case-insensitive way when it uses DNS name.
* tls: fix issue where OCSP was inadvertently removed from SSL response in multi-context scenarios.
* upstream: fix handling of moving endpoints between priorities when active health checks are enabled. Previously moving to a higher numbered priority was a NOOP, and moving to a lower numbered priority caused an abort.
* upstream: retry budgets will now set default values for xDS configurations.
* zipkin: fix 'verbose' mode to emit annotations for stream events. This was the documented behavior, but wasn't behaving as documented.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* access_logs: removed legacy unbounded access logs and runtime guard `envoy.reloadable_features.disallow_unbounded_access_logs`.
* dns: removed legacy buggy wildcard matching path and runtime guard `envoy.reloadable_features.fix_wildcard_matching`.
* dynamic_forward_proxy: removed `envoy.reloadable_features.enable_dns_cache_circuit_breakers` and legacy code path.
* http: removed legacy connect behavior and runtime guard `envoy.reloadable_features.stop_faking_paths`.
* http: removed legacy connection close behavior and runtime guard `envoy.reloadable_features.fixed_connection_close`.
* http: removed legacy HTTP/1.1 error reporting path and runtime guard `envoy.reloadable_features.early_errors_via_hcm`.
* http: removed legacy sanitization path for upgrade response headers and runtime guard `envoy.reloadable_features.fix_upgrade_response`.
* http: removed legacy date header overwriting logic and runtime guard `envoy.reloadable_features.preserve_upstream_date deprecation`.
* http: removed legacy ALPN handling and runtime guard `envoy.reloadable_features.http_default_alpn`.
* listener: removed legacy runtime guard `envoy.reloadable_features.listener_in_place_filterchain_update`.
* router: removed `envoy.reloadable_features.consume_all_retry_headers` and legacy code path.
* router: removed `envoy.reloadable_features.preserve_query_string_in_path_redirects` and legacy code path.

New Features
------------

* access log: added a new :ref:`OpenTelemetry access logger <envoy_v3_api_msg_extensions.access_loggers.open_telemetry.v3alpha.OpenTelemetryAccessLogConfig>` extension, allowing a flexible log structure with native Envoy access log formatting.
* access log: added the new response flag `NC` for upstream cluster not found. The error flag is set when the http or tcp route is found for the request but the cluster is not available.
* access log: added the :ref:`formatters <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.formatters>` extension point for custom formatters (command operators).
* access log: added support for cross platform writing to :ref:`standard output <envoy_v3_api_msg_extensions.access_loggers.stream.v3.StdoutAccessLog>` and :ref:`standard error <envoy_v3_api_msg_extensions.access_loggers.stream.v3.StderrAccessLog>`.
* access log: support command operator: %FILTER_CHAIN_NAME% for the downstream tcp and http request.
* access log: support command operator: %REQUEST_HEADERS_BYTES%, %RESPONSE_HEADERS_BYTES%, and %RESPONSE_TRAILERS_BYTES%.
* admin: added support for :ref:`access loggers <envoy_v3_api_msg_config.accesslog.v3.AccessLog>` to the admin interface.
* composite filter: added new :ref:`composite filter <config_http_filters_composite>` that can be used to instantiate different filter configuratios based on matching incoming data.
* compression: add brotli :ref:`compressor <envoy_v3_api_msg_extensions.compression.brotli.compressor.v3.Brotli>` and :ref:`decompressor <envoy_v3_api_msg_extensions.compression.brotli.decompressor.v3.Brotli>`.
* compression: extended the compression allow compressing when the content length header is not present. This behavior may be temporarily reverted by setting `envoy.reloadable_features.enable_compression_without_content_length_header` to false.
* config: add `envoy.features.fail_on_any_deprecated_feature` runtime key, which matches the behaviour of compile-time flag `ENVOY_DISABLE_DEPRECATED_FEATURES`, i.e. use of deprecated fields will cause a crash.
* config: the ``Node`` :ref:`dynamic context parameters <envoy_v3_api_field_config.core.v3.Node.dynamic_parameters>` are populated in discovery requests when set on the server instance.
* dispatcher: supports a stack of `Envoy::ScopeTrackedObject` instead of a single tracked object. This will allow Envoy to dump more debug information on crash.
* ext_authz: added :ref:`response_headers_to_add <envoy_v3_api_field_service.auth.v3.OkHttpResponse.response_headers_to_add>` to support sending response headers to downstream clients on OK authorization checks via gRPC.
* ext_authz: added :ref:`allowed_client_headers_on_success <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.AuthorizationResponse.allowed_client_headers_on_success>` to support sending response headers to downstream clients on OK external authorization checks via HTTP.
* grpc_json_transcoder: added :ref:`request_validation_options <envoy_v3_api_field_extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder.request_validation_options>` to reject invalid requests early.
* grpc_json_transcoder: filter can now be configured on per-route/per-vhost level as well. Leaving empty list of services in the filter configuration disables transcoding on the specific route.
* http: added support for `Envoy::ScopeTrackedObject` for HTTP/1 and HTTP/2 dispatching. Crashes while inside the dispatching loop should dump debug information. Furthermore, HTTP/1 and HTTP/2 clients now dumps the originating request whose response from the upstream caused Envoy to crash.
* http: added support for :ref:`preconnecting <envoy_v3_api_msg_config.cluster.v3.Cluster.PreconnectPolicy>`. Preconnecting is off by default, but recommended for clusters serving latency-sensitive traffic, especially if using HTTP/1.1.
* http: added support for stream filters to mutate the cached route set by HCM route resolution. Useful for filters in a filter chain that want to override specific methods/properties of a route. See :ref:`http route mutation <arch_overview_http_filters_route_mutation>` docs for more information.
* http: added new runtime config `envoy.reloadable_features.check_unsupported_typed_per_filter_config`, the default value is true. When the value is true, envoy will reject virtual host-specific typed per filter config when the filter doesn't support it.
* http: added the ability to preserve HTTP/1 header case across the proxy. See the :ref:`header casing <config_http_conn_man_header_casing>` documentation for more information.
* http: change frame flood and abuse checks to the upstream HTTP/2 codec to ON by default. It can be disabled by setting the `envoy.reloadable_features.upstream_http2_flood_checks` runtime key to false.
* http: hash multiple header values instead of only hash the first header value. It can be disabled by setting the `envoy.reloadable_features.hash_multiple_header_values` runtime key to false. See the :ref:`HashPolicy's Header configuration <envoy_v3_api_msg_config.route.v3.RouteAction.HashPolicy.Header>` for more information.
* json: introduced new JSON parser (https://github.com/nlohmann/json) to replace RapidJSON. The new parser is disabled by default. To test the new RapidJSON parser, enable the runtime feature `envoy.reloadable_features.remove_legacy_json`.
* kill_request: :ref:`Kill Request <config_http_filters_kill_request>` now supports bidirection killing.
* listener: added an optional :ref:`stat_prefix <envoy_v3_api_field_config.listener.v3.Listener.stat_prefix>`.
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
* thrift_proxy: added per upstream metrics within the :ref:`thrift router <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.router.v3.Router>` for messagetype counters in request/response.
* thrift_proxy: added per upstream metrics within the :ref:`thrift router <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.router.v3.Router>` for request time histograms.
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

* admin: :ref:`access_log_path <envoy_v3_api_field_config.bootstrap.v3.Admin.access_log_path>` is deprecated in favor for :ref:`access loggers <envoy_v3_api_msg_config.accesslog.v3.AccessLog>`.

