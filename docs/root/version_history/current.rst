1.17.0 (pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* config: v2 is now fatal-by-default. This may be overridden by setting :option:`--bootstrap-version` 2 on the CLI for a v2 bootstrap file and also enabling the runtime `envoy.reloadable_features.enable_deprecated_v2_api` feature. See
  the :ref:`FAQ entry <faq_api_version_transition>` for further details.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* build: the Alpine based debug images are no longer built in CI, use Ubuntu based images instead.
* cluster manager: the cluster which can't extract secret entity by SDS to be warming and never activate. This feature is disabled by default and is controlled by runtime guard `envoy.reloadable_features.cluster_keep_warming_no_secret_entity`.
* expr filter: added `connection.termination_details` property support.
* ext_authz filter: disable `envoy.reloadable_features.ext_authz_measure_timeout_on_check_created` by default.
* ext_authz filter: the deprecated field :ref:`use_alpha <envoy_api_field_config.filter.http.ext_authz.v2.ExtAuthz.use_alpha>` is no longer supported and cannot be set anymore.
* formatter: the :ref:`text_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.text_format>` field no longer requires at least one byte, and may now be the empty string. It has also become deprecated: see Deprecated section.
* grpc_web filter: if a `grpc-accept-encoding` header is present it's passed as-is to the upstream and if it isn't `grpc-accept-encoding:identity` is sent instead. The header was always overwriten with `grpc-accept-encoding:identity,deflate,gzip` before.
* http: upstream protocol will now only be logged if an upstream stream was established.
* jwt_authn filter: added support of Jwt time constraint verification with a clock skew (default to 60 seconds) and added a filter config field :ref:`clock_skew_seconds <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.clock_skew_seconds>` to configure it.
* kill_request: enable a way to configure kill header name in KillRequest proto.
* listener: injection of the :ref:`TLS inspector <config_listener_filters_tls_inspector>` has been disabled by default. This feature is controlled by the runtime guard `envoy.reloadable_features.disable_tls_inspector_injection`.
* memory: enable new tcmalloc with restartable sequences for aarch64 builds.
* mongo proxy metrics: swapped network connection remote and local closed counters previously set reversed (`cx_destroy_local_with_active_rq` and `cx_destroy_remote_with_active_rq`).
* outlier detection: added :ref:`max_ejection_time <envoy_v3_api_field_config.cluster.v3.OutlierDetection.max_ejection_time>` to limit ejection time growth when a node stays unhealthy for extended period of time. By default :ref:`max_ejection_time <envoy_v3_api_field_config.cluster.v3.OutlierDetection.max_ejection_time>` limits ejection time to 5 minutes. Additionally, when the node stays healthy, ejection time decreases. See :ref:`ejection algorithm<arch_overview_outlier_detection_algorithm>` for more info. Previously, ejection time could grow without limit and never decreased.
* performance: improve performance when handling large HTTP/1 bodies.
* tls: removed RSA key transport and SHA-1 cipher suites from the client-side defaults.
* watchdog: the watchdog action :ref:`abort_action <envoy_v3_api_msg_watchdog.v3alpha.AbortActionConfig>` is now the default action to terminate the process if watchdog kill / multikill is enabled.
* xds: to support TTLs, heartbeating has been added to xDS. As a result, responses that contain empty resources without updating the version will no longer be propagated to the
  subscribers. To undo this for VHDS (which is the only subscriber that wants empty resources), the `envoy.reloadable_features.vhds_heartbeats` can be set to "false".

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* config: validate that upgrade configs have a non-empty :ref:`upgrade_type <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.UpgradeConfig.upgrade_type>`, fixing a bug where an errant "-" could result in unexpected behavior.
* dns: fix a bug where custom resolvers provided in configuration were not preserved after network issues.
* dns_filter: correctly associate DNS response IDs when multiple queries are received.
* grpc mux: fix sending node again after stream is reset when ::ref:`set_node_on_first_message_only <envoy_api_field_core.ApiConfigSource.set_node_on_first_message_only>` is set.
* http: fixed URL parsing for HTTP/1.1 fully qualified URLs and connect requests containing IPv6 addresses.
* http: reject requests with missing required headers after filter chain processing.
* http: sending CONNECT_ERROR for HTTP/2 where appropriate during CONNECT requests.
* proxy_proto: fixed a bug where the wrong downstream address got sent to upstream connections.
* proxy_proto: fixed a bug where network filters would not have the correct downstreamRemoteAddress() when accessed from the StreamInfo. This could result in incorrect enforcement of RBAC rules in the RBAC network filter (but not in the RBAC HTTP filter), or incorrect access log addresses from tcp_proxy.
* sds: fix a bug that clusters sharing same sds target are marked active immediately.
* tls: fix detection of the upstream connection close event.
* tls: fix read resumption after triggering buffer high-watermark and all remaining request/response bytes are stored in the SSL connection's internal buffers.
* udp: fixed issue in which receiving truncated UDP datagrams would cause Envoy to crash.
* watchdog: touch the watchdog before most event loop operations to avoid misses when handling bursts of callbacks.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* dispatcher: removed legacy socket read/write resumption code path and runtime guard `envoy.reloadable_features.activate_fds_next_event_loop`.
* ext_authz: removed auto ignore case in HTTP-based `ext_authz` header matching and the runtime guard `envoy.reloadable_features.ext_authz_http_service_enable_case_sensitive_string_matcher`. To ignore case, set the :ref:`ignore_case <envoy_api_field_type.matcher.StringMatcher.ignore_case>` field to true.
* http: flip default HTTP/1 and HTTP/2 server codec implementations to new codecs that remove the use of exceptions for control flow. To revert to old codec behavior, set the runtime feature `envoy.reloadable_features.new_codec_behavior` to false.
* http: removed `envoy.reloadable_features.http1_flood_protection` and legacy code path for turning flood protection off.
* http: removed `envoy.reloadable_features.new_codec_behavior` and legacy codecs.

New Features
------------
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
* http: added :ref:`stripping any port from host header <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.strip_any_host_port>` support.
* http: clusters now support selecting HTTP/1 or HTTP/2 based on ALPN, configurable via :ref:`alpn_config <envoy_v3_api_field_extensions.upstreams.http.v3.HttpProtocolOptions.auto_config>` in the :ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>` message.
* jwt_authn: added support for :ref:`per-route config <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.PerRouteConfig>`.
* kill_request: added new :ref:`HTTP kill request filter <config_http_filters_kill_request>`.
* listener: added an optional :ref:`default filter chain <envoy_v3_api_field_config.listener.v3.Listener.default_filter_chain>`. If this field is supplied, and none of the :ref:`filter_chains <envoy_v3_api_field_config.listener.v3.Listener.filter_chains>` matches, this default filter chain is used to serve the connection.
* listener: added back the :ref:`use_original_dst field <envoy_v3_api_field_config.listener.v3.Listener.use_original_dst>`.
* log: added a new custom flag ``%_`` to the log pattern to print the actual message to log, but with escaped newlines.
* lua: added `downstreamDirectRemoteAddress()` and `downstreamLocalAddress()` APIs to :ref:`streamInfo() <config_http_filters_lua_stream_info_wrapper>`.
* mongo_proxy: the list of commands to produce metrics for is now :ref:`configurable <envoy_v3_api_field_extensions.filters.network.mongo_proxy.v3.MongoProxy.commands>`.
* network: added a :ref:`timeout <envoy_v3_api_field_config.listener.v3.FilterChain.transport_socket_connect_timeout>` for incoming connections completing transport-level negotiation, including TLS and ALTS hanshakes.
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
* thrift_proxy: added a new :ref: `payload_passthrough <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.ThriftProxy.payload_passthrough>` option to skip decoding body in the Thrift message.
* tls: added support for RSA certificates with 4096-bit keys in FIPS mode.
* tracing: added SkyWalking tracer.
* tracing: added support for setting the hostname used when sending spans to a Zipkin collector using the :ref:`collector_hostname <envoy_v3_api_field_config.trace.v3.ZipkinConfig.collector_hostname>` field.
* xds: added support for resource TTLs. A TTL is specified on the :ref:`Resource <envoy_api_msg_Resource>`. For SotW, a :ref:`Resource <envoy_api_msg_Resource>` can be embedded
  in the list of resources to specify the TTL.

Deprecated
----------
* cluster: HTTP configuration for upstream clusters has beem reworked. HTTP-specific configuration is now done in the new :ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>` message, configured via the cluster's :ref:`extension_protocol_options<envoy_v3_api_field_config.cluster.v3.Cluster.typed_extension_protocol_options>`. This replaces explicit HTTP configuration in cluster config, including :ref:`upstream_http_protocol_options<envoy_v3_api_field_config.cluster.v3.Cluster.upstream_http_protocol_options>` :ref:`common_http_protocol_options<envoy_v3_api_field_config.cluster.v3.Cluster.common_http_protocol_options>` :ref:`http_protocol_options<envoy_v3_api_field_config.cluster.v3.Cluster.http_protocol_options>` :ref:`http2_protocol_options<envoy_v3_api_field_config.cluster.v3.Cluster.http2_protocol_options>` and :ref:`protocol_selection<envoy_v3_api_field_config.cluster.v3.Cluster.protocol_selection>`. Examples of before-and-after configuration can be found in the :ref:`http_protocol_options docs <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>` and all of Envoy's example configurations have been updated to the new style of config.
* compression: the fields :ref:`content_length <envoy_v3_api_field_extensions.filters.http.compressor.v3.Compressor.content_length>`, :ref:`content_type <envoy_v3_api_field_extensions.filters.http.compressor.v3.Compressor.content_type>`, :ref:`disable_on_etag_header <envoy_v3_api_field_extensions.filters.http.compressor.v3.Compressor.disable_on_etag_header>`, :ref:`remove_accept_encoding_header <envoy_v3_api_field_extensions.filters.http.compressor.v3.Compressor.remove_accept_encoding_header>` and :ref:`runtime_enabled <envoy_v3_api_field_extensions.filters.http.compressor.v3.Compressor.runtime_enabled>` of the :ref:`Compressor <envoy_v3_api_msg_extensions.filters.http.compressor.v3.Compressor>` message have been deprecated in favor of :ref:`response_direction_config <envoy_v3_api_field_extensions.filters.http.compressor.v3.Compressor.response_direction_config>`.
* formatter: :ref:`text_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.text_format>` is now deprecated in favor of :ref:`text_format_source <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.text_format_source>`. To migrate existing text format strings, use the :ref:`inline_string <envoy_v3_api_field_config.core.v3.DataSource.inline_string>` field.
* gzip: :ref:`HTTP Gzip filter <config_http_filters_gzip>` is rejected now unless explicitly allowed with :ref:`runtime override <config_runtime_deprecation>` `envoy.deprecated_features.allow_deprecated_gzip_http_filter` set to `true`.
* listener: :ref:`use_proxy_proto <envoy_v3_api_field_config.listener.v3.FilterChain.use_proxy_proto>` has been deprecated in favor of adding a :ref:`PROXY protocol listener filter <config_listener_filters_proxy_protocol>` explicitly.
* logging: the `--log-format-prefix-with-location` option is removed.
* ratelimit: the :ref:`dynamic metadata <envoy_v3_api_field_config.route.v3.RateLimit.Action.dynamic_metadata>` action is deprecated in favor of the more generic :ref:`metadata <envoy_v3_api_field_config.route.v3.RateLimit.Action.metadata>` action.
* stats: the `--use-fake-symbol-table` option is removed.
