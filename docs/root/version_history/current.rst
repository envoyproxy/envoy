1.17.0 (pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* build: the Alpine based debug images are no longer built in CI, use Ubuntu based images instead.
* ext_authz filter: disable `envoy.reloadable_features.ext_authz_measure_timeout_on_check_created` by default.
* ext_authz filter: the deprecated field :ref:`use_alpha <envoy_api_field_config.filter.http.ext_authz.v2.ExtAuthz.use_alpha>` is no longer supported and cannot be set anymore.
* grpc_web filter: if a `grpc-accept-encoding` header is present it's passed as-is to the upstream and if it isn't `grpc-accept-encoding:identity` is sent instead. The header was always overwriten with `grpc-accept-encoding:identity,deflate,gzip` before.
* watchdog: the watchdog action :ref:`abort_action <envoy_v3_api_msg_watchdog.v3alpha.AbortActionConfig>` is now the default action to terminate the process if watchdog kill / multikill is enabled.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* http: sending CONNECT_ERROR for HTTP/2 where appropriate during CONNECT requests.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* ext_authz: removed auto ignore case in HTTP-based `ext_authz` header matching and the runtime guard `envoy.reloadable_features.ext_authz_http_service_enable_case_sensitive_string_matcher`. To ignore case, set the :ref:`ignore_case <envoy_api_field_type.matcher.StringMatcher.ignore_case>` field to true.
* http: flip default HTTP/1 and HTTP/2 server codec implementations to new codecs that remove the use of exceptions for control flow. To revert to old codec behavior, set the runtime feature `envoy.reloadable_features.new_codec_behavior` to false.
* http: removed `envoy.reloadable_features.http1_flood_protection` and legacy code path for turning flood protection off.

New Features
------------
* access log: added a :ref:`dynamic metadata filter<envoy_v3_api_msg_config.accesslog.v3.MetadataFilter>` for access logs, which filters whether to log based on matching dynamic metadata.
* access log: added support for :ref:`%DOWNSTREAM_PEER_FINGERPRINT_1% <config_access_log_format_response_flags>` as a response flag.
* access log: added support for nested objects in :ref:`JSON logging mode <config_access_log_format_dictionaries>`.
* access log: added :ref:`omit_empty_values<envoy_v3_api_field_config.core.v3.SubstitutionFormatString.omit_empty_values>` option to omit unset value from formatted log.
* admin: added the ability to dump init manager unready targets information :ref:`/init_dump <operations_admin_interface_init_dump>` and :ref:`/init_dump?mask={} <operations_admin_interface_init_dump_by_mask>`.
* build: enable building envoy :ref:`arm64 images <arm_binaries>` by buildx tool in x86 CI platform.
* cluster: added :ref:`connection_pool_idle_timeout <envoy_v3_api_field_config.cluster.v3.Cluster.connection_pool_idle_timeout>`, which controls how long the cluster manager waits before deleting a pool that has seen no activity. The original and default behavior is that pools will not be deleted unless a limit would be exceeded.
* cluster: added new :ref:`connection_pool_per_downstream_connection <envoy_v3_api_field_config.cluster.v3.Cluster.connection_pool_per_downstream_connection>` flag, which enable creation of a new connection pool for each downstream connection.
* decompressor filter: reports compressed and uncompressed bytes in trailers.
* dns_filter: added support for answering :ref:`service record<envoy_v3_api_msg_data.dns.v3.DnsTable.DnsService>` queries.
* dynamic_forward_proxy: added :ref:`use_tcp_for_dns_lookups<envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.use_tcp_for_dns_lookups>` option to use TCP for DNS lookups in order to match the DNS options for :ref:`Clusters<envoy_v3_api_msg_config.cluster.v3.Cluster>`.
* ext_authz filter: added support for emitting dynamic metadata for both :ref:`HTTP <config_http_filters_ext_authz_dynamic_metadata>` and :ref:`network <config_network_filters_ext_authz_dynamic_metadata>` filters.
  The emitted dynamic metadata is set by :ref:`dynamic metadata <envoy_v3_api_field_service.auth.v3.CheckResponse.dynamic_metadata>` field in a returned :ref:`CheckResponse <envoy_v3_api_msg_service.auth.v3.CheckResponse>`.
* grpc-json: support specifying `response_body` field in for `google.api.HttpBody` message.
* hds: added :ref:`cluster_endpoints_health <envoy_v3_api_field_service.health.v3.EndpointHealthResponse.cluster_endpoints_health>` to HDS responses, keeping endpoints in the same groupings as they were configured in the HDS specifier by cluster and locality instead of as a flat list.
* hds: added :ref:`transport_socket_matches <envoy_v3_api_field_service.health.v3.ClusterHealthCheck.transport_socket_matches>` to HDS cluster health check specifier, so the existing match filter :ref:`transport_socket_match_criteria <envoy_v3_api_field_config.core.v3.HealthCheck.transport_socket_match_criteria>` in the repeated field :ref:`health_checks <envoy_v3_api_field_service.health.v3.ClusterHealthCheck.health_checks>` has context to match against. This unblocks support for health checks over HTTPS and HTTP/2.
* http: added support for :ref:`%DOWNSTREAM_PEER_FINGERPRINT_1% <config_http_conn_man_headers_custom_request_headers>` as custom header.
* http: added :ref:`allow_chunked_length <envoy_v3_api_field_config.core.v3.Http1ProtocolOptions.allow_chunked_length>` configuration option for HTTP/1 codec to allow processing requests/responses with both Content-Length and Transfer-Encoding: chunked headers. If such message is served and option is enabled - per RFC Content-Length is ignored and removed.
* http: introduced new HTTP/1 and HTTP/2 codec implementations that will remove the use of exceptions for control flow due to high risk factors and instead use error statuses. The old behavior is used by default, but the new codecs can be enabled for testing by setting the runtime feature `envoy.reloadable_features.new_codec_behavior` to true. The new codecs will be in development for one month, and then enabled by default while the old codecs are deprecated.
* load balancer: added :ref:`RingHashLbConfig<envoy_v3_api_msg_config.cluster.v3.Cluster.MaglevLbConfig>` to configure the table size of Maglev consistent hash.
* load balancer: added a :ref:`configuration<envoy_v3_api_msg_config.cluster.v3.Cluster.LeastRequestLbConfig>` option to specify the active request bias used by the least request load balancer.
* load balancer: added an :ref:`option <envoy_v3_api_field_config.cluster.v3.Cluster.LbSubsetConfig.LbSubsetSelector.single_host_per_subset>` to optimize subset load balancing when there is only one host per subset.
* load balancer: added support for bounded load per host for consistent hash load balancers via :ref:`hash_balance_factor <envoy_api_field_Cluster.CommonLbConfig.consistent_hashing_lb_config>`.
* lua: added Lua APIs to access :ref:`SSL connection info <config_http_filters_lua_ssl_socket_info>` object.
* lua: added Lua API for :ref:`base64 escaping a string <config_http_filters_lua_stream_handle_api_base64_escape>`.
* lua: added new :ref:`source_code <envoy_v3_api_field_extensions.filters.http.lua.v3.LuaPerRoute.source_code>` field to support the dispatching of inline Lua code in per route configuration of Lua filter.
* overload management: add :ref:`scaling <envoy_v3_api_field_config.overload.v3.Trigger.scaled>` trigger for OverloadManager actions.
* postgres network filter: :ref:`metadata <config_network_filters_postgres_proxy_dynamic_metadata>` is produced based on SQL query.
* ratelimit: added :ref:`enable_x_ratelimit_headers <envoy_v3_api_msg_extensions.filters.http.ratelimit.v3.RateLimit>` option to enable `X-RateLimit-*` headers as defined in `draft RFC <https://tools.ietf.org/id/draft-polli-ratelimit-headers-03.html>`_.
* ratelimit: added support for optional :ref:`descriptor_key <envoy_v3_api_field_config.route.v3.RateLimit.Action.generic_key>` to Generic Key action.
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
* tcp: switched the TCP connection pool to the new "shared" connection pool, sharing a common code base with HTTP and HTTP/2. Any unexpected behavioral changes can be temporarily reverted by setting `envoy.reloadable_features.new_tcp_connection_pool` to false.
* tcp_proxy: allow earlier network filters to set metadataMatchCriteria on the connection StreamInfo to influence load balancing.
* tls: switched from using socket BIOs to using custom BIOs that know how to interact with IoHandles. The feature can be disabled by setting runtime feature `envoy.reloadable_features.tls_use_io_handle_bio` to false.
* tracing: added ability to set some :ref:`optional segment fields<envoy_v3_api_field_config.trace.v3.XRayConfig.segment_fields>` in the AWS  X-Ray tracer.
* udp_proxy: added :ref:`hash_policies <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig>` to support hash based routing.
* udp_proxy: added :ref:`use_original_src_ip <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig>` option to replicate the downstream remote address of the packets on the upstream side of Envoy. It is similar to :ref:`original source filter <envoy_v3_api_msg_extensions.filters.listener.original_src.v3.OriginalSrc>`.
* watchdog: support randomizing the watchdog's kill timeout to prevent synchronized kills via a maximium jitter parameter :ref:`max_kill_timeout_jitter<envoy_v3_api_field_config.bootstrap.v3.Watchdog.max_kill_timeout_jitter>`.
* watchdog: supports an extension point where actions can be registered to fire on watchdog events such as miss, megamiss, kill and multikill. See ref:`watchdog actions<envoy_v3_api_field_config.bootstrap.v3.Watchdog.actions>`.
* watchdog: watchdog action extension that does cpu profiling. See ref:`Profile Action <envoy_v3_api_file_envoy/extensions/watchdog/profile_action/v3alpha/profile_action.proto>`.
* xds: added :ref:`extension config discovery<envoy_v3_api_msg_config.core.v3.ExtensionConfigSource>` support for HTTP filters.
* zlib: added option to use `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ as zlib library.
* config: added new runtime feature `envoy.features.enable_all_deprecated_features` that allows the use of all deprecated features.
* grpc: implemented header value syntax support when defining :ref:`initial metadata <envoy_v3_api_field_config.core.v3.GrpcService.initial_metadata>` for gRPC-based `ext_authz` :ref:`HTTP <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.grpc_service>` and :ref:`network <envoy_v3_api_field_extensions.filters.network.ext_authz.v3.ExtAuthz.grpc_service>` filters, and :ref:`ratelimit <envoy_v3_api_field_config.ratelimit.v3.RateLimitServiceConfig.grpc_service>` filters.
* hds: added support for delta updates in the :ref:`HealthCheckSpecifier <envoy_v3_api_msg_service.health.v3.HealthCheckSpecifier>`, making only the Endpoints and Health Checkers that changed be reconstructed on receiving a new message, rather than the entire HDS.
* health_check: added option to use :ref:`no_traffic_healthy_interval <envoy_v3_api_field_config.core.v3.HealthCheck.no_traffic_healthy_interval>` which allows a different no traffic interval when the host is healthy.
* listener: added an optional :ref:`default filter chain <envoy_v3_api_field_config.listener.v3.Listener.default_filter_chain>`. If this field is supplied, and none of the :ref:`filter_chains <envoy_v3_api_field_config.listener.v3.Listener.filter_chains>` matches, this default filter chain is used to serve the connection.
* lua: added `downstreamDirectRemoteAddress()` and `downstreamLocalAddress()` APIs to :ref:`streamInfo() <config_http_filters_lua_stream_info_wrapper>`.
* mongo_proxy: the list of commands to produce metrics for is now :ref:`configurable <envoy_v3_api_field_extensions.filters.network.mongo_proxy.v3.MongoProxy.commands>`.
* ratelimit: added support for use of various :ref:`metadata <envoy_v3_api_field_config.route.v3.RateLimit.Action.metadata>` as a ratelimit action.
* ratelimit: added :ref:`disable_x_envoy_ratelimited_header <envoy_v3_api_msg_extensions.filters.http.ratelimit.v3.RateLimit>` option to disable `X-Envoy-RateLimited` header.
* tcp: added a new :ref:`envoy.overload_actions.reject_incoming_connections <config_overload_manager_overload_actions>` action to reject incoming TCP connections.

Deprecated
----------
* gzip: :ref:`HTTP Gzip filter <config_http_filters_gzip>` is rejected now unless explicitly allowed with :ref:`runtime override <config_runtime_deprecation>` `envoy.deprecated_features.allow_deprecated_gzip_http_filter` set to `true`.
* ratelimit: the :ref:`dynamic metadata <envoy_v3_api_field_config.route.v3.RateLimit.Action.dynamic_metadata>` action is deprecated in favor of the more generic :ref:`metadata <envoy_v3_api_field_config.route.v3.RateLimit.Action.metadata>` action.
