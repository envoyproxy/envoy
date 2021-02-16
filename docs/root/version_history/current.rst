1.18.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* grpc_stats: the default value for :ref:`stats_for_all_methods <envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.stats_for_all_methods>` is switched
  from true to false, in order to avoid possible memory exhaustion due to an untrusted downstream sending a large number of unique method names. The previous
  default value was deprecated in version 1.14.0. This only changes the behavior when the value is not set. The previous behavior can be used by setting the value
  to true. This behavior change by be overridden by setting runtime feature `envoy.deprecated_features.grpc_stats_filter_enable_stats_for_all_methods_by_default`.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

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
* http: allow to use path canonicalizer from `googleurl <https://quiche.googlesource.com/googleurl>`_
  instead of `//source/common/chromium_url`. The new path canonicalizer is enabled by default. To
  revert to the legacy path canonicalizer, enable the runtime flag
  `envoy.reloadable_features.remove_forked_chromium_url`.
* http: increase the maximum allowed number of initial connection WINDOW_UPDATE frames sent by the peer from 1 to 5.
* http: upstream flood and abuse checks increment the count of opened HTTP/2 streams when Envoy sends
  initial HEADERS frame for the new stream. Before the counter was incrementred when Envoy received
  response HEADERS frame with the END_HEADERS flag set from upstream server.
* oauth filter: added the optional parameter :ref:`auth_scopes <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.auth_scopes>` with default value of 'user' if not provided. Enables this value to be overridden in the Authorization request to the OAuth provider.
* perf: allow reading more bytes per operation from raw sockets to improve performance.
* router: extended custom date formatting to DOWNSTREAM_PEER_CERT_V_START and DOWNSTREAM_PEER_CERT_V_END when using :ref:`custom request/response header formats <config_http_conn_man_headers_custom_request_headers>`.
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
* buffer: tighten network connection read and write buffer high watermarks in preparation to more careful enforcement of read limits. Buffer high-watermark is now set to the exact configured value; previously it was set to value + 1.
* fault injection: stop counting as active fault after delay elapsed. Previously fault injection filter continues to count the injected delay as an active fault even after it has elapsed. This produces incorrect output statistics and impacts the max number of consecutive faults allowed (e.g., for long-lived streams). This change decreases the active fault count when the delay fault is the only active and has gone finished.
* grpc-web: fix local reply and non-proto-encoded gRPC response handling for small response bodies. This fix can be temporarily reverted by setting `envoy.reloadable_features.grpc_web_fix_non_proto_encoded_response_handling` to false.
* http: disallowing "host:" in request_headers_to_add for behavioral consistency with rejecting :authority header. This behavior can be temporarily reverted by setting `envoy.reloadable_features.treat_host_like_authority` to false.
* http: reverting a behavioral change where upstream connect timeouts were temporarily treated differently from other connection failures. The change back to the original behavior can be temporarily reverted by setting `envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure` to false.
* listener: prevent crashing when an unknown listener config proto is received and debug logging is enabled.
* overload: fix a bug that can cause use-after-free when one scaled timer disables another one with the same duration.
* upstream: fix handling of moving endpoints between priorities when active health checks are enabled. Previously moving to a higher numbered priority was a NOOP, and moving to a lower numbered priority caused an abort.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* access_logs: removed legacy unbounded access logs and runtime guard `envoy.reloadable_features.disallow_unbounded_access_logs`.
* dns: removed legacy buggy wildcard matching path and runtime guard `envoy.reloadable_features.fix_wildcard_matching`.
* dynamic_forward_proxy: removed `envoy.reloadable_features.enable_dns_cache_circuit_breakers` and legacy code path.
* http: removed legacy connection close behavior and runtime guard `envoy.reloadable_features.fixed_connection_close`.
* http: removed legacy HTTP/1.1 error reporting path and runtime guard `envoy.reloadable_features.early_errors_via_hcm`.
* http: removed legacy sanitization path for upgrade response headers and runtime guard `envoy.reloadable_features.fix_upgrade_response`.
* http: removed legacy date header overwriting logic and runtime guard `envoy.reloadable_features.preserve_upstream_date deprecation`.
* listener: removed legacy runtime guard `envoy.reloadable_features.listener_in_place_filterchain_update`.
* router: removed `envoy.reloadable_features.consume_all_retry_headers` and legacy code path.

New Features
------------

* access log: added the :ref:`formatters <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.formatters>` extension point for custom formatters (command operators).
* access log: support command operator: %REQUEST_HEADERS_BYTES%, %RESPONSE_HEADERS_BYTES%, and %RESPONSE_TRAILERS_BYTES%.
* compression: add brotli :ref:`compressor <envoy_v3_api_msg_extensions.compression.brotli.compressor.v3.Brotli>` and :ref:`decompressor <envoy_v3_api_msg_extensions.compression.brotli.decompressor.v3.Brotli>`.
* config: add `envoy.features.fail_on_any_deprecated_feature` runtime key, which matches the behaviour of compile-time flag `ENVOY_DISABLE_DEPRECATED_FEATURES`, i.e. use of deprecated fields will cause a crash.
* dispatcher: supports a stack of `Envoy::ScopeTrackedObject` instead of a single tracked object. This will allow Envoy to dump more debug information on crash.
* ext_authz: added :ref:`response_headers_to_add <envoy_v3_api_field_service.auth.v3.OkHttpResponse.response_headers_to_add>` to support sending response headers to downstream clients on OK authorization checks via gRPC.
* ext_authz: added :ref:`allowed_client_headers_on_success <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.AuthorizationResponse.allowed_client_headers_on_success>` to support sending response headers to downstream clients on OK external authorization checks via HTTP.
* grpc_json_transcoder: added :ref:`request_validation_options <envoy_v3_api_field_extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder.request_validation_options>` to reject invalid requests early.
* grpc_json_transcoder: filter can now be configured on per-route/per-vhost level as well. Leaving empty list of services in the filter configuration disables transcoding on the specific route.
* http: added support for `Envoy::ScopeTrackedObject` for HTTP/1 dispatching. Crashes while inside the dispatching loop should dump debug information.
* http: added support for :ref:`preconnecting <envoy_v3_api_msg_config.cluster.v3.Cluster.PreconnectPolicy>`. Preconnecting is off by default, but recommended for clusters serving latency-sensitive traffic, especially if using HTTP/1.1.
* http: change frame flood and abuse checks to the upstream HTTP/2 codec to ON by default. It can be disabled by setting the `envoy.reloadable_features.upstream_http2_flood_checks` runtime key to false.
* json: introduced new JSON parser (https://github.com/nlohmann/json) to replace RapidJSON. The new parser is disabled by default. To test the new RapidJSON parser, enable the runtime feature `envoy.reloadable_features.remove_legacy_json`.
* overload: add support for scaling :ref:`transport connection timeouts<envoy_v3_api_enum_value_config.overload.v3.ScaleTimersOverloadActionConfig.TimerType.TRANSPORT_SOCKET_CONNECT>`. This can be used to reduce the TLS handshake timeout in response to overload.
* postgres: added ability to :ref:`terminate SSL<envoy_v3_api_field_extensions.filters.network.postgres_proxy.v3alpha.PostgresProxy.terminate_ssl>`.
* route config: added :ref:`allow_post field <envoy_v3_api_field_config.route.v3.RouteAction.UpgradeConfig.ConnectConfig.allow_post>` for allowing POST payload as raw TCP.
* route config: added :ref:`max_direct_response_body_size_bytes <envoy_v3_api_field_config.route.v3.RouteConfiguration.max_direct_response_body_size_bytes>` to set maximum :ref:`direct response body <envoy_v3_api_field_config.route.v3.DirectResponseAction.body>` size in bytes. If not specified the default remains 4096 bytes.
* server: added *fips_mode* to :ref:`server compilation settings <server_compilation_settings_statistics>` related statistic.
* tcp_proxy: add support for converting raw TCP streams into HTTP/1.1 CONNECT requests. See :ref:`upgrade documentation <tunneling-tcp-over-http>` for details.
* tcp_proxy: added a :ref:`use_post field <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.TunnelingConfig.use_post>` for using HTTP POST to proxy TCP streams.
* tcp_proxy: added a :ref:`headers_to_add field <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.TunnelingConfig.headers_to_add>` for setting additional headers to the HTTP requests for TCP proxing.

Deprecated
----------
