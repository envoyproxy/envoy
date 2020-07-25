1.16.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* compressor: always insert `Vary` headers for compressible resources even if it's decided not to compress a response due to incompatible `Accept-Encoding` value. The `Vary` header needs to be inserted to let a caching proxy in front of Envoy know that the requested resource still can be served with compression applied.
* http: added :ref:`headers_to_add <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.headers_to_add>` to :ref:`local reply mapper <config_http_conn_man_local_reply>` to allow its users to add/append/override response HTTP headers to local replies.
* http: added HCM level configuration of :ref:`error handling on invalid messaging <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stream_error_on_invalid_http_message>` which substantially changes Envoy's behavior when encountering invalid HTTP/1.1 defaulting to closing the connection instead of allowing reuse. This can temporarily be reverted by setting `envoy.reloadable_features.hcm_stream_error_on_invalid_message` to false, or permanently reverted by setting the :ref:`HCM option <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stream_error_on_invalid_http_message>` to true to restore prior HTTP/1.1 beavior and setting the *new* HTTP/2 configuration :ref:`override_stream_error_on_invalid_http_message <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.override_stream_error_on_invalid_http_message>` to false to retain prior HTTP/2 behavior.
* http: clarified and enforced 1xx handling. Multiple 100-continue headers are coalesced when proxying. 1xx headers other than {100, 101} are dropped.
* http: fixed the 100-continue response path to properly handle upstream failure by sending 5xx responses. This behavior can be temporarily reverted by setting `envoy.reloadable_features.allow_500_after_100` to false.
* http: the per-stream FilterState maintained by the HTTP connection manager will now provide read/write access to the downstream connection FilterState. As such, code that relies on interacting with this might
  see a change in behavior.
* logging: nghttp2 log messages no longer appear at trace level unless `ENVOY_NGHTTP2_TRACE` is set
  in the environment.
* router: now consumes all retry related headers to prevent them from being propagated to the upstream. This behavior may be reverted by setting runtime feature `envoy.reloadable_features.consume_all_retry_headers` to false.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* csrf: fixed issues with regards to origin and host header parsing.
* dynamic_forward_proxy: only perform DNS lookups for routes to Dynamic Forward Proxy clusters since other cluster types handle DNS lookup themselves.
* fault: fixed an issue with `active_faults` gauge not being decremented for when abort faults were injected.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed legacy header sanitization and the runtime guard `envoy.reloadable_features.strict_header_validation`.
* http: removed legacy transfer-encoding enforcement and runtime guard `envoy.reloadable_features.reject_unsupported_transfer_encodings`.
* http: removed configurable strict host validation and runtime guard `envoy.reloadable_features.strict_authority_validation`.

New Features
------------

* access loggers: added GRPC_STATUS operator on logging format.
* access loggers: extened specifier for FilterStateFormatter to output :ref:`unstructured log string <config_access_log_format_filter_state>`.
* access loggers: file access logger config added :ref:`log_format <envoy_v3_api_field_extensions.access_loggers.file.v3.FileAccessLog.log_format>`.
* access loggers: gRPC access logger config added added :ref:`API version <envoy_v3_api_field_extensions.access_loggers.grpc.v3.CommonGrpcAccessLogConfig.transport_api_version>` to explicitly set the version of gRPC service endpoint and message to be used.
* admin: added support for dumping EDS config at :ref:`/config_dump?include_eds <operations_admin_interface_config_dump_include_eds>`.
* aggregate cluster: make route :ref:`retry_priority <envoy_v3_api_field_config.route.v3.RetryPolicy.retry_priority>` predicates work with :ref:`this cluster type <envoy_v3_api_msg_extensions.clusters.aggregate.v3.ClusterConfig>`.
* build: official released binary is now built on Ubuntu 18.04, requires glibc >= 2.27.
* build: official released binary is now built with Clang 10.0.0.
* cluster: added an extension point for configurable :ref:`upstreams <envoy_v3_api_field_config.cluster.v3.Cluster.upstream_config>`.
* compressor: generic :ref:`compressor <config_http_filters_compressor>` filter exposed to users.
* config: added :ref:`identifier <config_cluster_manager_cds>` stat that reflects control plane identifier.
* config: added :ref:`version_text <config_cluster_manager_cds>` stat that reflects xDS version.
* decompressor: generic :ref:`decompressor <config_http_filters_decompressor>` filter exposed to users.
* dynamic forward proxy: added :ref:`SNI based dynamic forward proxy <config_network_filters_sni_dynamic_forward_proxy>` support.
* dynamic forward proxy: added :ref:`allow_insecure_cluster_options<envoy_v3_api_field_extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig.allow_insecure_cluster_options>` to allow disabling of auto_san_validation and auto_sni.
* ext_authz filter: added :ref:`v2 deny_at_disable <envoy_api_field_config.filter.http.ext_authz.v2.ExtAuthz.deny_at_disable>`, :ref:`v3 deny_at_disable <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.deny_at_disable>`. This allows to force deny for protected path while filter gets disabled, by setting this key to true.
* ext_authz filter: added API version field for both :ref:`HTTP <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.transport_api_version>`
  and :ref:`Network <envoy_v3_api_field_extensions.filters.network.ext_authz.v3.ExtAuthz.transport_api_version>` filters to explicitly set the version of gRPC service endpoint and message to be used.
* ext_authz filter: added :ref:`v3 allowed_upstream_headers_to_append <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.AuthorizationResponse.allowed_upstream_headers_to_append>` to allow appending multiple header entries (returned by the authorization server) with the same key to the original request headers.
* fault: added support for controlling the percentage of requests that abort, delay and response rate limits faults
  are applied to using :ref:`HTTP headers <config_http_filters_fault_injection_http_header>` to the HTTP fault filter.
* fault: added support for specifying grpc_status code in abort faults using
  :ref:`HTTP header <config_http_filters_fault_injection_http_header>` or abort fault configuration in HTTP fault filter.
* filter: add `upstram_rq_time` stats to the GPRC stats filter.
  Disabled by default and can be enabled via :ref:`enable_upstream_stats <envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.enable_upstream_stats>`.
* grpc: added support for Google gRPC :ref:`custom channel arguments <envoy_v3_api_field_config.core.v3.GrpcService.GoogleGrpc.channel_args>`.
* grpc-json: added support for streaming response using
  `google.api.HttpBody <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto>`_.
* grpc-json: send a `x-envoy-original-method` header to grpc services.
* gzip filter: added option to set zlib's next output buffer size.
* hds: updated to allow to explicitly set the API version of gRPC service endpoint and message to be used.
* header to metadata: added support for regex substitutions on header values.
* health checks: allow configuring health check transport sockets by specifying :ref:`transport socket match criteria <envoy_v3_api_field_config.core.v3.HealthCheck.transport_socket_match_criteria>`.
* http: added :ref:`local_reply config <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.local_reply_config>` to http_connection_manager to customize :ref:`local reply <config_http_conn_man_local_reply>`.
* http: added :ref:`stripping port from host header <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.strip_matching_host_port>` support.
* http: added support for proxying CONNECT requests, terminating CONNECT requests, and converting raw TCP streams into HTTP/2 CONNECT requests. See :ref:`upgrade documentation<arch_overview_upgrades>` for details.
* listener: added in place filter chain update flow for tcp listener update which doesn't close connections if the corresponding network filter chain is equivalent during the listener update.
  Can be disabled by setting runtime feature `envoy.reloadable_features.listener_in_place_filterchain_update` to false.
  Also added additional draining filter chain stat for :ref:`listener manager <config_listener_manager_stats>` to track the number of draining filter chains and the number of in place update attempts.
* load balancing: :ref:`least requests load balancing <arch_overview_load_balancing_types_least_request>` was improved to check each host at most one time per pick attempt.
* logger: added :ref:`--log-format-prefix-with-location <operations_cli>` command line option to prefix '%v' with file path and line number.
* logger: added :option:`--log-format-prefix-with-location` command line option to prefix '%v' with file path and line number.
* lrs: added new *envoy_api_field_service.load_stats.v2.LoadStatsResponse.send_all_clusters* field
  in LRS response, which allows management servers to avoid explicitly listing all clusters it is
  interested in; behavior is allowed based on new "envoy.lrs.supports_send_all_clusters" capability
  in :ref:`client_features<envoy_v3_api_field_config.core.v3.Node.client_features>` field.
* lrs: updated to allow to explicitly set the API version of gRPC service endpoint and message to be used.
* lua: added tracing to the ``httpCall()`` API.
* metrics service: added added :ref:`API version <envoy_v3_api_field_config.metrics.v3.MetricsServiceConfig.transport_api_version>` to explicitly set the version of gRPC service endpoint and message to be used.
* network filters: added a :ref:`postgres proxy filter <config_network_filters_postgres_proxy>`.
* network filters: added a :ref:`rocketmq proxy filter <config_network_filters_rocketmq_proxy>`.
* ratelimit: add support for use of dynamic metadata :ref:`dynamic_metadata <envoy_v3_api_field_config.route.v3.RateLimit.Action.dynamic_metadata>` as a ratelimit action.
* ratelimit: added :ref:`API version <envoy_v3_api_field_config.ratelimit.v3.RateLimitServiceConfig.transport_api_version>` to explicitly set the version of gRPC service endpoint and message to be used.
* redis: added acl support :ref:`downstream_auth_username <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.downstream_auth_username>` for downstream client ACL authentication, and :ref:`auth_username <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProtocolOptions.auth_username>` to configure authentication usernames for upstream Redis 6+ server clusters with ACL enabled.
* regex: added support for enforcing max program size via runtime and stats to monitor program size for :ref:`Google RE2 <envoy_v3_api_field_type.matcher.v3.RegexMatcher.GoogleRE2.max_program_size>`.
* request_id: added to :ref:`always_set_request_id_in_response setting <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.always_set_request_id_in_response>`
  to set :ref:`x-request-id <config_http_conn_man_headers_x-request-id>` header in response even if
  tracing is not forced.
* router: add support for RESPONSE_FLAGS and RESPONSE_CODE_DETAILS :ref:`header formatters
  <config_http_conn_man_headers_custom_request_headers>`.
* router: allow Rate Limiting Service to be called in case of missing request header for a descriptor if the :ref:`skip_if_absent <envoy_v3_api_field_config.route.v3.RateLimit.Action.RequestHeaders.skip_if_absent>` field is set to true.
* router: more fine grained internal redirect configs are added to the :ref:`internal_redirect_policy
  <envoy_v3_api_field_config.route.v3.RouteAction.internal_redirect_policy>` field.
* runtime: add new gauge :ref:`deprecated_feature_seen_since_process_start <runtime_stats>` that gets reset across hot restarts.
* server: add the option :option:`--drain-strategy` to enable different drain strategies for DrainManager::drainClose().
* stats: added the option to :ref:`report counters as deltas <envoy_v3_api_field_config.metrics.v3.MetricsServiceConfig.report_counters_as_deltas>` to the metrics service stats sink.
* tracing: tracing configuration has been made fully dynamic and every HTTP connection manager
  can now have a separate :ref:`tracing provider <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.Tracing.provider>`.
* udp: :ref:`udp_proxy <config_udp_listener_filters_udp_proxy>` filter has been upgraded to v3 and is no longer considered alpha.
* access log: added support for :ref:`%DOWNSTREAM_PEER_FINGERPRINT_1% <config_access_log_format_response_flags>` as a response flag.
* dynamic_forward_proxy: added :ref:`use_tcp_for_dns_lookups<envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.use_tcp_for_dns_lookups>` option to use TCP for DNS lookups in order to match the DNS options for :ref:`Clusters<envoy_v3_api_msg_config.cluster.v3.Cluster>`.
* ext_authz filter: added support for emitting dynamic metadata for both :ref:`HTTP <config_http_filters_ext_authz_dynamic_metadata>` and :ref:`network <config_network_filters_ext_authz_dynamic_metadata>` filters.
* grpc-json: support specifying `response_body` field in for `google.api.HttpBody` message.
* http: added support for :ref:`%DOWNSTREAM_PEER_FINGERPRINT_1% <config_http_conn_man_headers_custom_request_headers>` as custom header.
* http: introduced new HTTP/1 and HTTP/2 codec implementations that will remove the use of exceptions for control flow due to high risk factors and instead use error statuses. The old behavior is deprecated, but can be used during the removal period by setting the runtime feature `envoy.reloadable_features.new_codec_behavior` to false. The removal period will be one month.
* load balancer: added a :ref:`configuration<envoy_v3_api_msg_config.cluster.v3.Cluster.LeastRequestLbConfig>` option to specify the active request bias used by the least request load balancer.
* redis: added fault injection support :ref:`fault injection for redis proxy <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.faults>`, described further in :ref:`configuration documentation <config_network_filters_redis_proxy>`.
* router: added new
  :ref:`envoy-ratelimited<config_http_filters_router_retry_policy-envoy-ratelimited>`
  retry policy, which allows retrying envoy's own rate limited responses.
* stats: added optional histograms to :ref:`cluster stats <config_cluster_manager_cluster_stats_request_response_sizes>`
  that track headers and body sizes of requests and responses.
* stats: allow configuring histogram buckets for stats sinks and admin endpoints that support it.
* tap: added :ref:`generic body matcher<envoy_v3_api_msg_config.tap.v3.HttpGenericBodyMatch>` to scan http requests and responses for text or hex patterns.
* tcp: switched the TCP connection pool to the new "shared" connection pool, sharing a common code base with HTTP and HTTP/2. Any unexpected behavioral changes can be temporarily reverted by setting `envoy.reloadable_features.new_tcp_connection_pool` to false.
* xds: added :ref:`extension config discovery<envoy_v3_api_msg_config.core.v3.ExtensionConfigSource>` support for HTTP filters.

Deprecated
----------
* The :ref:`track_timeout_budgets <envoy_v3_api_field_config.cluster.v3.Cluster.track_timeout_budgets>`
  field has been deprecated in favor of `timeout_budgets` part of an :ref:`Optional Configuration <envoy_v3_api_field_config.cluster.v3.Cluster.track_cluster_stats>`.
