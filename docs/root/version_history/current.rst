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
* oauth filter: added the optional parameter :ref:`auth_scopes <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.auth_scopes>` with default value of 'user' if not provided. Enables this value to be overridden in the Authorization request to the OAuth provider.
* perf: allow reading more bytes per operation from raw sockets to improve performance.
* tcp: setting NODELAY in the base connection class. This should have no effect for TCP or HTTP proxying, but may improve throughput in other areas. This behavior can be temporarily reverted by setting `envoy.reloadable_features.always_nodelay` to false.
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
* grpc-web: fix local reply and non-proto-encoded gRPC response handling for small response bodies. This fix can be temporarily reverted by setting `envoy.reloadable_features.grpc_web_fix_non_proto_encoded_response_handling` to false.
* http: disallowing "host:" in request_headers_to_add for behavioral consistency with rejecting :authority header. This behavior can be temporarily reverted by setting `envoy.reloadable_features.treat_host_like_authority` to false.
* http: reverting a behavioral change where upstream connect timeouts were temporarily treated differently from other connection failures. The change back to the original behavior can be temporarily reverted by setting `envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure` to false.
* listener: prevent crashing when an unknown listener config proto is received and debug logging is enabled.
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
* router: removed `envoy.reloadable_features.consume_all_retry_headers` and legacy code path.

New Features
------------
* access log: added the :ref:`formatters <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.formatters>` extension point for custom formatters (command operators).
* access log: support command operator: %REQUEST_HEADERS_BYTES%, %RESPONSE_HEADERS_BYTES%, and %RESPONSE_TRAILERS_BYTES%.
* dispatcher: supports a stack of `Envoy::ScopeTrackedObject` instead of a single tracked object. This will allow Envoy to dump more debug information on crash.
* grpc_json_transcoder: added option :ref:`strict_http_request_validation <envoy_v3_api_field_extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder.strict_http_request_validation>` to reject invalid requests early.
* grpc_json_transcoder: filter can now be configured on per-route/per-vhost level as well. Leaving empty list of services in the filter configuration disables transcoding on the specific route.
* http: added support for :ref:`preconnecting <envoy_v3_api_msg_config.cluster.v3.Cluster.PreconnectPolicy>`. Preconnecting is off by default, but recommended for clusters serving latency-sensitive traffic, especially if using HTTP/1.1.
* http: change frame flood and abuse checks to the upstream HTTP/2 codec to ON by default. It can be disabled by setting the `envoy.reloadable_features.upstream_http2_flood_checks` runtime key to false.
* overload: add support for scaling :ref:`transport connection timeouts<envoy_v3_api_enum_value_config.overload.v3.ScaleTimersOverloadActionConfig.TimerType.TRANSPORT_SOCKET_CONNECT>`. This can be used to reduce the TLS handshake timeout in response to overload.
* server: added *fips_mode* to :ref:`server compilation settings <server_compilation_settings_statistics>` related statistic.
* tcp_proxy: add support for converting raw TCP streams into HTTP/1.1 CONNECT requests. See :ref:`upgrade documentation <tunneling-tcp-over-http>` for details.
* compression: the :ref:`compressor <envoy_v3_api_msg_extensions.filters.http.compressor.v3.Compressor>` filter adds support for compressing request payloads. Its configuration is unified with the :ref:`decompressor <envoy_v3_api_msg_extensions.filters.http.decompressor.v3.Decompressor>` filter with two new fields for different directions - :ref:`requests <envoy_v3_api_field_extensions.filters.http.compressor.v3.Compressor.request_direction_config>` and :ref:`responses <envoy_v3_api_field_extensions.filters.http.compressor.v3.Compressor.response_direction_config>`. The latter deprecates the old response-specific fields and, if used, roots the response-specific stats in `<stat_prefix>.compressor.<compressor_library.name>.<compressor_library_stat_prefix>.response.*` instead of `<stat_prefix>.compressor.<compressor_library.name>.<compressor_library_stat_prefix>.*`.
* config: added ability to flush stats when the admin's :ref:`/stats endpoint <operations_admin_interface_stats>` is hit instead of on a timer via :ref:`stats_flush_on_admin <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.stats_flush_on_admin>`.
* config: added new runtime feature `envoy.features.enable_all_deprecated_features` that allows the use of all deprecated features.
* formatter: added new :ref:`text_format_source <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.text_format_source>` field to support format strings both inline and from a file.
* grpc: implemented header value syntax support when defining :ref:`initial metadata <envoy_v3_api_field_config.core.v3.GrpcService.initial_metadata>` for gRPC-based `ext_authz` :ref:`HTTP <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.grpc_service>` and :ref:`network <envoy_v3_api_field_extensions.filters.network.ext_authz.v3.ExtAuthz.grpc_service>` filters, and :ref:`ratelimit <envoy_v3_api_field_config.ratelimit.v3.RateLimitServiceConfig.grpc_service>` filters.
* grpc-json: added support for configuring :ref:`unescaping behavior <envoy_v3_api_field_extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder.url_unescape_spec>` for path components.
* hds: added support for delta updates in the :ref:`HealthCheckSpecifier <envoy_v3_api_msg_service.health.v3.HealthCheckSpecifier>`, making only the Endpoints and Health Checkers that changed be reconstructed on receiving a new message, rather than the entire HDS.
* health_check: added option to use :ref:`no_traffic_healthy_interval <envoy_v3_api_field_config.core.v3.HealthCheck.no_traffic_healthy_interval>` which allows a different no traffic interval when the host is healthy.
* http: added HCM :ref:`timeout config field <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.request_headers_timeout>` to control how long a downstream has to finish sending headers before the stream is cancelled.
* http: added frame flood and abuse checks to the upstream HTTP/2 codec. This check is off by default and can be enabled by setting the `envoy.reloadable_features.upstream_http2_flood_checks` runtime key to true.
* http: clusters now support selecting HTTP/1 or HTTP/2 based on ALPN, configurable via :ref:`alpn_config <envoy_v3_api_field_extensions.upstreams.http.v3.HttpProtocolOptions.auto_config>` in the :ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>` message.
* jwt_authn: added support for :ref:`per-route config <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.PerRouteConfig>`.
* kill_request: added new :ref:`HTTP kill request filter <config_http_filters_kill_request>`.
* listener: added an optional :ref:`default filter chain <envoy_v3_api_field_config.listener.v3.Listener.default_filter_chain>`. If this field is supplied, and none of the :ref:`filter_chains <envoy_v3_api_field_config.listener.v3.Listener.filter_chains>` matches, this default filter chain is used to serve the connection.
* listener: added back the :ref:`use_original_dst field <envoy_v3_api_field_config.listener.v3.Listener.use_original_dst>`.
* log: added a new custom flag ``%_`` to the log pattern to print the actual message to log, but with escaped newlines.
* lua: added `downstreamDirectRemoteAddress()` and `downstreamLocalAddress()` APIs to :ref:`streamInfo() <config_http_filters_lua_stream_info_wrapper>`.
* mongo_proxy: the list of commands to produce metrics for is now :ref:`configurable <envoy_v3_api_field_extensions.filters.network.mongo_proxy.v3.MongoProxy.commands>`.
* network: added a :ref:`timeout <envoy_v3_api_field_config.listener.v3.FilterChain.transport_socket_connect_timeout>` for incoming connections completing transport-level negotiation, including TLS and ALTS hanshakes.
* network: added a :ref:`use_post field <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TunnelingConfig.use_post>` for using HTTP/2 POST to proxy TCP streams.
* network: added a :ref:`headers field <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TunnelingConfig.headers>` for setting additional headers to the HTTP requests for TCP proxing.
* overload: add :ref:`envoy.overload_actions.reduce_timeouts <config_overload_manager_overload_actions>` overload action to enable scaling timeouts down with load. Scaling support :ref:`is limited <envoy_v3_api_enum_config.overload.v3.ScaleTimersOverloadActionConfig.TimerType>` to the HTTP connection and stream idle timeouts.
* ratelimit: added support for use of various :ref:`metadata <envoy_v3_api_field_config.route.v3.RateLimit.Action.metadata>` as a ratelimit action.
* ratelimit: added :ref:`disable_x_envoy_ratelimited_header <envoy_v3_api_msg_extensions.filters.http.ratelimit.v3.RateLimit>` option to disable `X-Envoy-RateLimited` header.
* ratelimit: added :ref:`body <envoy_v3_api_field_service.ratelimit.v3.RateLimitResponse.raw_body>` field to support custom response bodies for non-OK responses from the external ratelimit service.
* router: added support for regex rewrites during HTTP redirects using :ref:`regex_rewrite <envoy_v3_api_field_config.route.v3.RedirectAction.regex_rewrite>`.
* sds: improved support for atomic :ref:`key rotations <xds_certificate_rotation>` and added configurable rotation triggers for
  :ref:`TlsCertificate <envoy_v3_api_field_extensions.transport_sockets.tls.v3.TlsCertificate.watched_directory>` and
  :ref:`CertificateValidationContext <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.watched_directory>`.
* signal: added an extension point for custom actions to run on the thread that has encountered a fatal error. Actions are configurable via :ref:`fatal_actions <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.fatal_actions>`.
* start_tls: :ref:`transport socket<envoy_v3_api_msg_extensions.transport_sockets.starttls.v3.StartTlsConfig>` which starts in clear-text but may programatically be converted to use tls.
* tcp: added a new :ref:`envoy.overload_actions.reject_incoming_connections <config_overload_manager_overload_actions>` action to reject incoming TCP connections.
* tcp_post: added new :ref:`HTTP TCP Post filter <config_http_filters_tcp_post>`.
* thrift_proxy: added a new :ref: `payload_passthrough <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.ThriftProxy.payload_passthrough>` option to skip decoding body in the Thrift message.
* tls: added support for RSA certificates with 4096-bit keys in FIPS mode.
* tracing: added SkyWalking tracer.
* tracing: added support for setting the hostname used when sending spans to a Zipkin collector using the :ref:`collector_hostname <envoy_v3_api_field_config.trace.v3.ZipkinConfig.collector_hostname>` field.
* xds: added support for resource TTLs. A TTL is specified on the :ref:`Resource <envoy_api_msg_Resource>`. For SotW, a :ref:`Resource <envoy_api_msg_Resource>` can be embedded
  in the list of resources to specify the TTL.

Deprecated
----------
