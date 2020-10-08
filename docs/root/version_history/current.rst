1.17.0 (pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------
* access log: added a :ref:`dynamic metadata filter<envoy_v3_api_msg_config.accesslog.v3.MetadataFilter>` for access logs, which filters whether to log based on matching dynamic metadata.
* access log: added support for :ref:`%DOWNSTREAM_PEER_FINGERPRINT_1% <config_access_log_format_response_flags>` as a response flag.
* access log: added support for :ref:`%CONNECTION_TERMINATION_DETAILS% <config_access_log_format_connection_termination_details>` as a log command operator about why the connection is terminated by Envoy.
* access log: added support for nested objects in :ref:`JSON logging mode <config_access_log_format_dictionaries>`.
* access log: added :ref:`omit_empty_values<envoy_v3_api_field_config.core.v3.SubstitutionFormatString.omit_empty_values>` option to omit unset value from formatted log.
* access log: added support for :ref:`%CONNECTION_ID% <config_access_log_format_connection_id>` for the downstream connection identifier.
* admin: added :ref:`circuit breakers settings <envoy_v3_api_msg_config.cluster.v3.CircuitBreakers>` information to GET /clusters?format=json :ref:`cluster status <envoy_v3_api_msg_admin.v3.ClusterStatus>`.
* admin: added :ref:`node <envoy_v3_api_msg_config.core.v3.Node>` information to GET /server_info :ref:`response object <envoy_v3_api_msg_admin.v3.ServerInfo>`.
* admin: added the ability to dump init manager unready targets information :ref:`/init_dump <operations_admin_interface_init_dump>` and :ref:`/init_dump?mask={} <operations_admin_interface_init_dump_by_mask>`.
* admission control: added the :ref:`admission control <envoy_v3_api_msg_extensions.filters.http.admission_control.v3alpha.AdmissionControl>` filter for client-side request throttling.
* build: enable building envoy :ref:`arm64 images <arm_binaries>` by buildx tool in x86 CI platform.
* cluster: added new :ref:`connection_pool_per_downstream_connection <envoy_v3_api_field_config.cluster.v3.Cluster.connection_pool_per_downstream_connection>` flag, which enable creation of a new connection pool for each downstream connection.
* decompressor filter: reports compressed and uncompressed bytes in trailers.
* dns: added support for doing DNS resolution using Apple's DnsService APIs in Apple platforms (macOS, iOS). This feature is ON by default, and is only configurable via the `envoy.restart_features.use_apple_api_for_dns_lookups` runtime key. Note that this value is latched during server startup and changing the runtime key is a no-op during the lifetime of the process.
* dns_filter: added support for answering :ref:`service record<envoy_v3_api_msg_data.dns.v3.DnsTable.DnsService>` queries.
* dynamic_forward_proxy: added :ref:`use_tcp_for_dns_lookups<envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.use_tcp_for_dns_lookups>` option to use TCP for DNS lookups in order to match the DNS options for :ref:`Clusters<envoy_v3_api_msg_config.cluster.v3.Cluster>`.
* ext_authz filter: added support for emitting dynamic metadata for both :ref:`HTTP <config_http_filters_ext_authz_dynamic_metadata>` and :ref:`network <config_network_filters_ext_authz_dynamic_metadata>` filters.
  The emitted dynamic metadata is set by :ref:`dynamic metadata <envoy_v3_api_field_service.auth.v3.CheckResponse.dynamic_metadata>` field in a returned :ref:`CheckResponse <envoy_v3_api_msg_service.auth.v3.CheckResponse>`.
* ext_authz filter: added :ref:`stat_prefix <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.stat_prefix>` as an optional additional prefix for the statistics emitted from `ext_authz` HTTP filter.
* ext_authz filter: added support for enabling the filter based on :ref:`dynamic metadata <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.filter_enabled_metadata>`.
* ext_authz filter: added support for letting the authorization server instruct Envoy to remove headers from the original request by setting the new field :ref:`headers_to_remove <envoy_v3_api_field_service.auth.v3.OkHttpResponse.headers_to_remove>` before forwarding it to the upstream.
* ext_authz filter: added support for sending :ref:`raw bytes as request body <envoy_v3_api_field_service.auth.v3.AttributeContext.HttpRequest.raw_body>` of a gRPC check request by setting :ref:`pack_as_bytes <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.BufferSettings.pack_as_bytes>` to true.
* ext_authz_filter: added :ref:`disable_request_body_buffering <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.CheckSettings.disable_request_body_buffering>` to disable request data buffering per-route.
* grpc-json: support specifying `response_body` field in for `google.api.HttpBody` message.
* hds: added :ref:`cluster_endpoints_health <envoy_v3_api_field_service.health.v3.EndpointHealthResponse.cluster_endpoints_health>` to HDS responses, keeping endpoints in the same groupings as they were configured in the HDS specifier by cluster and locality instead of as a flat list.
* hds: added :ref:`transport_socket_matches <envoy_v3_api_field_service.health.v3.ClusterHealthCheck.transport_socket_matches>` to HDS cluster health check specifier, so the existing match filter :ref:`transport_socket_match_criteria <envoy_v3_api_field_config.core.v3.HealthCheck.transport_socket_match_criteria>` in the repeated field :ref:`health_checks <envoy_v3_api_field_service.health.v3.ClusterHealthCheck.health_checks>` has context to match against. This unblocks support for health checks over HTTPS and HTTP/2.
* hot restart: added :option:`--socket-path` and :option:`--socket-mode` to configure UDS path in the filesystem and set permission to it.
* http: added HTTP/2 support for :ref:`connection keepalive <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.connection_keepalive>` via PING.
* http: added support for :ref:`%DOWNSTREAM_PEER_FINGERPRINT_1% <config_http_conn_man_headers_custom_request_headers>` as custom header.
* http: added :ref:`allow_chunked_length <envoy_v3_api_field_config.core.v3.Http1ProtocolOptions.allow_chunked_length>` configuration option for HTTP/1 codec to allow processing requests/responses with both Content-Length and Transfer-Encoding: chunked headers. If such message is served and option is enabled - per RFC Content-Length is ignored and removed.
* http: added :ref:`CDN Loop filter <envoy_v3_api_msg_extensions.filters.http.cdn_loop.v3alpha.CdnLoopConfig>` and :ref:`documentation <config_http_filters_cdn_loop>`.
* http: added :ref:`MaxStreamDuration proto <envoy_v3_api_msg_config.route.v3.RouteAction.MaxStreamDuration>` for configuring per-route downstream duration timeouts.
* http: introduced new HTTP/1 and HTTP/2 codec implementations that will remove the use of exceptions for control flow due to high risk factors and instead use error statuses. The old behavior is used by default for HTTP/1.1 and HTTP/2 server connections. The new codecs can be enabled for testing by setting the runtime feature `envoy.reloadable_features.new_codec_behavior` to true. The new codecs will be in development for one month, and then enabled by default while the old codecs are deprecated.
* http: modified the HTTP header-map data-structure to use an underlying dictionary and a list (no change to the header-map API). To conform with previous versions, the use of a dictionary is currently disabled. It can be enabled by setting the `envoy.http.headermap.lazy_map_min_size` runtime feature to a non-negative number which defines the minimal number of headers in a request/response/trailers required for using a dictionary in addition to the list. Our current benchmarks suggest that the value 3 is a good threshold for most workloads.
* load balancer: added :ref:`RingHashLbConfig<envoy_v3_api_msg_config.cluster.v3.Cluster.MaglevLbConfig>` to configure the table size of Maglev consistent hash.
* load balancer: added a :ref:`configuration<envoy_v3_api_msg_config.cluster.v3.Cluster.LeastRequestLbConfig>` option to specify the active request bias used by the least request load balancer.
* load balancer: added an :ref:`option <envoy_v3_api_field_config.cluster.v3.Cluster.LbSubsetConfig.LbSubsetSelector.single_host_per_subset>` to optimize subset load balancing when there is only one host per subset.
* load balancer: added support for bounded load per host for consistent hash load balancers via :ref:`hash_balance_factor <envoy_api_field_Cluster.CommonLbConfig.consistent_hashing_lb_config>`.
* local_ratelimit: added new :ref:`HTTP local ratelimit filter <config_http_filters_local_rate_limit>`.
* local_reply config: added :ref:`content_type<envoy_v3_api_field_config.core.v3.SubstitutionFormatString.content_type>` field to set content-type.
* lua: added Lua APIs to access :ref:`SSL connection info <config_http_filters_lua_ssl_socket_info>` object.
* lua: added Lua API for :ref:`base64 escaping a string <config_http_filters_lua_stream_handle_api_base64_escape>`.
* lua: added Lua API for :ref:`setting the current buffer content <config_http_filters_lua_buffer_wrapper_api_set_bytes>`.
* lua: added new :ref:`source_code <envoy_v3_api_field_extensions.filters.http.lua.v3.LuaPerRoute.source_code>` field to support the dispatching of inline Lua code in per route configuration of Lua filter.
* overload management: add :ref:`scaling <envoy_v3_api_field_config.overload.v3.Trigger.scaled>` trigger for OverloadManager actions.
* postgres network filter: :ref:`metadata <config_network_filters_postgres_proxy_dynamic_metadata>` is produced based on SQL query.
* proxy protocol: added support for generating the header upstream using :ref:`Proxy Protocol Transport Socket <extension_envoy.transport_sockets.upstream_proxy_protocol>`.
* ratelimit: added :ref:`enable_x_ratelimit_headers <envoy_v3_api_msg_extensions.filters.http.ratelimit.v3.RateLimit>` option to enable `X-RateLimit-*` headers as defined in `draft RFC <https://tools.ietf.org/id/draft-polli-ratelimit-headers-03.html>`_.
* ratelimit: added :ref:`per route config <envoy_v3_api_msg_extensions.filters.http.ratelimit.v3.RateLimitPerRoute>` for rate limit filter.
* ratelimit: added support for optional :ref:`descriptor_key <envoy_v3_api_field_config.route.v3.RateLimit.Action.generic_key>` to Generic Key action.
* rbac filter: added the name of the matched policy to the response code detail when a request is rejected by the RBAC filter.
* rbac filter: added a log action to the :ref:`RBAC filter <envoy_v3_api_msg_config.rbac.v3.RBAC>` which sets dynamic metadata to inform access loggers whether to log.
* redis: added fault injection support :ref:`fault injection for redis proxy <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.faults>`, described further in :ref:`configuration documentation <config_network_filters_redis_proxy>`.
* router: added a new :ref:`rate limited retry back off <envoy_v3_api_msg_config.route.v3.RetryPolicy.RateLimitedRetryBackOff>` strategy that uses headers like `Retry-After` or `X-RateLimit-Reset` to decide the back off interval.
* router: added new
  :ref:`envoy-ratelimited<config_http_filters_router_retry_policy-envoy-ratelimited>`
  retry policy, which allows retrying envoy's own rate limited responses.
* router: added new :ref:`host_rewrite_path_regex <envoy_v3_api_field_config.route.v3.RouteAction.host_rewrite_path_regex>`
  option, which allows rewriting Host header based on path.
* router: added support for DYNAMIC_METADATA :ref:`header formatter <config_http_conn_man_headers_custom_request_headers>`.
* router_check_tool: added support for `request_header_matches`, `response_header_matches` to :ref:`router check tool <config_tools_router_check_tool>`.
* signal: added support for calling fatal error handlers without envoy's signal handler, via FatalErrorHandler::callFatalErrorHandlers().
* stats: added optional histograms to :ref:`cluster stats <config_cluster_manager_cluster_stats_request_response_sizes>`
  that track headers and body sizes of requests and responses.
* stats: allow configuring histogram buckets for stats sinks and admin endpoints that support it.
* tap: added :ref:`generic body matcher<envoy_v3_api_msg_config.tap.v3.HttpGenericBodyMatch>` to scan http requests and responses for text or hex patterns.
* tcp_proxy: added :ref:`max_downstream_connection_duration<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.max_downstream_connection_duration>` for downstream connection. When max duration is reached the connection will be closed.
* tcp_proxy: allow earlier network filters to set metadataMatchCriteria on the connection StreamInfo to influence load balancing.
* tls: added OCSP stapling support through the :ref:`ocsp_staple <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.TlsCertificate>` and :ref:`ocsp_staple_policy <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.DownstreamTlsContext>` configuration options. See :ref:`OCSP Stapling <arch_overview_ssl_ocsp_stapling>` for usage and runtime flags.
* tls: introduce new :ref:`extension point<envoy_v3_api_field_extensions.transport_sockets.tls.v3.CommonTlsContext.custom_handshaker>` for overriding :ref:`TLS handshaker <arch_overview_ssl>` behavior.
* tls: switched from using socket BIOs to using custom BIOs that know how to interact with IoHandles. The feature can be disabled by setting runtime feature `envoy.reloadable_features.tls_use_io_handle_bio` to false.
* tracing: added ability to set some :ref:`optional segment fields<envoy_v3_api_field_config.trace.v3.XRayConfig.segment_fields>` in the AWS  X-Ray tracer.
* udp_proxy: added :ref:`hash_policies <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig>` to support hash based routing.
* udp_proxy: added :ref:`use_original_src_ip <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig>` option to replicate the downstream remote address of the packets on the upstream side of Envoy. It is similar to :ref:`original source filter <envoy_v3_api_msg_extensions.filters.listener.original_src.v3.OriginalSrc>`.
* watchdog: support randomizing the watchdog's kill timeout to prevent synchronized kills via a maximium jitter parameter :ref:`max_kill_timeout_jitter<envoy_v3_api_field_config.bootstrap.v3.Watchdog.max_kill_timeout_jitter>`.
* watchdog: supports an extension point where actions can be registered to fire on watchdog events such as miss, megamiss, kill and multikill. See :ref:`watchdog actions<envoy_v3_api_field_config.bootstrap.v3.Watchdog.actions>`.
* watchdog: watchdog action extension that does cpu profiling. See :ref:`Profile Action <envoy_v3_api_file_envoy/extensions/watchdog/profile_action/v3alpha/profile_action.proto>`.
* watchdog: watchdog action extension that sends SIGABRT to the stuck thread to terminate the process. See :ref:`Abort Action <envoy_v3_api_file_envoy/extensions/watchdog/abort_action/v3alpha/abort_action.proto>`.
* xds: added :ref:`extension config discovery<envoy_v3_api_msg_config.core.v3.ExtensionConfigSource>` support for HTTP filters.
* xds: added support for mixed v2/v3 discovery response, which enable type url downgrade and upgrade. This feature is disabled by default and is controlled by runtime guard `envoy.reloadable_features.enable_type_url_downgrade_and_upgrade`.
* zlib: added option to use `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ as zlib library.

Deprecated
----------

