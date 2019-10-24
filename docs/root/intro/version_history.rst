Version history
---------------

1.12.0 (pending)
================
* access log: added a new flag for :ref:`downstream protocol error <envoy_api_field_data.accesslog.v2.ResponseFlags.downstream_protocol_error>`.
* access log: added :ref:`buffering <envoy_api_field_config.accesslog.v2.CommonGrpcAccessLogConfig.buffer_size_bytes>` and :ref:`periodical flushing <envoy_api_field_config.accesslog.v2.CommonGrpcAccessLogConfig.buffer_flush_interval>` support to gRPC access logger. Defaults to 16KB buffer and flushing every 1 second.
* access log: added DOWNSTREAM_DIRECT_REMOTE_ADDRESS and DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT :ref:`access log formatters <config_access_log_format>` and gRPC access logger.
* access log: gRPC Access Log Service (ALS) support added for :ref:`TCP access logs <envoy_api_msg_config.accesslog.v2.TcpGrpcAccessLogConfig>`.
* access log: reintroduce :ref:`filesystem <filesystem_stats>` stats and added the `write_failed` counter to track failed log writes
* admin: added ability to configure listener :ref:`socket options <envoy_api_field_config.bootstrap.v2.Admin.socket_options>`.
* admin: added config dump support for Secret Discovery Service :ref:`SecretConfigDump <envoy_api_msg_admin.v2alpha.SecretsConfigDump>`.
* admin: added support for :ref:`draining <operations_admin_interface_drain>` listeners via admin interface.
* admin: added :http:get:`/stats/recentlookups`, :http:post:`/stats/recentlookups/clear`,
   :http:post:`/stats/recentlookups/disable`, and :http:post:`/stats/recentlookups/enable` endpoints.
* api: added ::ref:`set_node_on_first_message_only <envoy_api_field_core.ApiConfigSource.set_node_on_first_message_only>` option to omit the node identifier from the subsequent discovery requests on the same stream.
* buffer filter: the buffer filter populates content-length header if not present, behavior can be disabled using the runtime feature `envoy.reloadable_features.buffer_filter_populate_content_length`.
* config: added support for :ref:`delta xDS <arch_overview_dynamic_config_delta>` (including ADS) delivery
* config: enforcing that terminal filters (e.g. HttpConnectionManager for L4, router for L7) be the last in their respective filter chains.
* config: added access log :ref:`extension filter<envoy_api_field_config.filter.accesslog.v2.AccessLogFilter.extension_filter>`.
* config: added support for :option:`--reject-unknown-dynamic-fields`, providing independent control
  over whether unknown fields are rejected in static and dynamic configuration. By default, unknown
  fields in static configuration are rejected and are allowed in dynamic configuration. Warnings
  are logged for the first use of any unknown field and these occurrences are counted in the
  :ref:`server.static_unknown_fields <server_statistics>` and :ref:`server.dynamic_unknown_fields
  <server_statistics>` statistics.
* config: async data access for local and remote data source.
* config: changed the default value of :ref:`initial_fetch_timeout <envoy_api_field_core.ConfigSource.initial_fetch_timeout>` from 0s to 15s. This is a change in behaviour in the sense that Envoy will move to the next initialization phase, even if the first config is not delivered in 15s. Refer to :ref:`initialization process <arch_overview_initialization>` for more details.
* config: added stat :ref:`init_fetch_timeout <config_cluster_manager_cds>`.
* csrf: add PATCH to supported methods.
* dns: added support for configuring :ref:`dns_failure_refresh_rate <envoy_api_field_Cluster.dns_failure_refresh_rate>` to set the DNS refresh rate during failures.
* ext_authz: added :ref:`configurable ability <envoy_api_field_config.filter.http.ext_authz.v2.ExtAuthz.metadata_context_namespaces>` to send dynamic metadata to the `ext_authz` service.
* ext_authz: added tracing to the HTTP client.
* fault: added overrides for default runtime keys in :ref:`HTTPFault <envoy_api_msg_config.filter.http.fault.v2.HTTPFault>` filter.
* grpc: added :ref:`AWS IAM grpc credentials extension <envoy_api_file_envoy/config/grpc_credential/v2alpha/aws_iam.proto>` for AWS-managed xDS.
* grpc: added :ref:`gRPC stats filter <config_http_filters_grpc_stats>` for collecting stats about gRPC calls and streaming message counts.
* grpc-json: added support for :ref:`ignoring unknown query parameters<envoy_api_field_config.filter.http.transcoder.v2.GrpcJsonTranscoder.ignore_unknown_query_parameters>`.
* grpc-json: added support for :ref:`the grpc-status-details-bin header<envoy_api_field_config.filter.http.transcoder.v2.GrpcJsonTranscoder.convert_grpc_status>`.
* header to metadata: added :ref:`PROTOBUF_VALUE <envoy_api_enum_value_config.filter.http.header_to_metadata.v2.Config.ValueType.PROTOBUF_VALUE>` and :ref:`ValueEncode <envoy_api_enum_config.filter.http.header_to_metadata.v2.Config.ValueEncode>` to support protobuf Value and Base64 encoding.
* http: added a default one hour idle timeout to upstream and downstream connections. HTTP connections with no stream and no activity will be closed after one hour unless the default idle_timeout overridden. To disable upstream idle timeouts, set the :ref:`idle_timeout <envoy_api_field_core.HttpProtocolOptions.idle_timeout>` to zero in Cluster :ref:`http_protocol_options<envoy_api_field_Cluster.common_http_protocol_options>`. To disable downstream idle timeouts, either set :ref:`idle_timeout <envoy_api_field_core.HttpProtocolOptions.idle_timeout>` to zero in the HttpConnectionManager :ref:`common_http_protocol_options <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.common_http_protocol_options>` or set the deprecated :ref:`connection manager <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.idle_timeout>` field to zero.
* http: added the ability to reject HTTP/1.1 requests with invalid HTTP header values, using the runtime feature `envoy.reloadable_features.strict_header_validation`.
* http: changed Envoy to forward existing x-forwarded-proto from upstream trusted proxies. Guarded by `envoy.reloadable_features.trusted_forwarded_proto` which defaults true.
* http: added the ability to configure the behavior of the server response header, via the :ref:`server_header_transformation<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.server_header_transformation>` field.
* http: added the ability to :ref:`merge adjacent slashes<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.merge_slashes>` in the path.
* http: :ref:`AUTO <envoy_api_enum_value_config.filter.network.http_connection_manager.v2.HttpConnectionManager.CodecType.AUTO>` codec protocol inference now requires the H2 magic bytes to be the first bytes transmitted by a downstream client.
* http: remove h2c upgrade headers for HTTP/1 as h2c upgrades are currently not supported.
* http: absolute URL support is now on by default. The prior behavior can be reinstated by setting :ref:`allow_absolute_url <envoy_api_field_core.Http1ProtocolOptions.allow_absolute_url>` to false.
* http: support :ref:`host rewrite <envoy_api_msg_config.filter.http.dynamic_forward_proxy.v2alpha.PerRouteConfig>` in the dynamic forward proxy.
* http: support :ref:`disabling the filter per route <envoy_api_msg_config.filter.http.grpc_http1_reverse_bridge.v2alpha1.FilterConfigPerRoute>` in the grpc http1 reverse bridge filter.
* listeners: added :ref:`continue_on_listener_filters_timeout <envoy_api_field_Listener.continue_on_listener_filters_timeout>` to configure whether a listener will still create a connection when listener filters time out.
* listeners: added :ref:`HTTP inspector listener filter <config_listener_filters_http_inspector>`.
* listeners: added :ref:`connection balancer <envoy_api_field_Listener.connection_balance_config>`
  configuration for TCP listeners.
* lua: extended `httpCall()` and `respond()` APIs to accept headers with entry values that can be a string or table of strings.
* lua: extended `dynamicMetadata:set()` to allow setting complex values
* metrics_service: added support for flushing histogram buckets.
* outlier_detector: added :ref:`support for the grpc-status response header <arch_overview_outlier_detection_grpc>` by mapping it to HTTP status. Guarded by envoy.reloadable_features.outlier_detection_support_for_grpc_status which defaults to true.
* performance: new buffer implementation enabled by default (to disable add "--use-libevent-buffers 1" to the command-line arguments when starting Envoy).
* performance: stats symbol table implementation (disabled by default; to test it, add "--use-fake-symbol-table 0" to the command-line arguments when starting Envoy).
* rbac: added support for DNS SAN as :ref:`principal_name <envoy_api_field_config.rbac.v2.Principal.Authenticated.principal_name>`.
* redis: added :ref:`enable_command_stats <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.ConnPoolSettings.enable_command_stats>` to enable :ref:`per command statistics <arch_overview_redis_cluster_command_stats>` for upstream clusters.
* redis: added :ref:`read_policy <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.ConnPoolSettings.read_policy>` to allow reading from redis replicas for Redis Cluster deployments.
* redis: fix a bug where the redis health checker ignored the upstream auth password.
* redis: enable_hashtaging is always enabled when the upstream uses open source Redis cluster protocol.
* regex: introduce new :ref:`RegexMatcher <envoy_api_msg_type.matcher.RegexMatcher>` type that
  provides a safe regex implementation for untrusted user input. This type is now used in all
  configuration that processes user provided input. See :ref:`deprecated configuration details
  <deprecated>` for more information.
* rbac: added conditions to the policy, see :ref:`condition <envoy_api_field_config.rbac.v2.Policy.condition>`.
* router: added :ref:`rq_retry_skipped_request_not_complete <config_http_filters_router_stats>` counter stat to router stats.
* router: :ref:`Scoped routing <arch_overview_http_routing_route_scope>` is supported.
* router: added new :ref:`retriable-headers <config_http_filters_router_x-envoy-retry-on>` retry policy. Retries can now be configured to trigger by arbitrary response header matching.
* router: added ability for most specific header mutations to take precedence, see :ref:`route configuration's most specific
  header mutations wins flag <envoy_api_field_RouteConfiguration.most_specific_header_mutations_wins>`
* router: added :ref:`respect_expected_rq_timeout <envoy_api_field_config.filter.http.router.v2.Router.respect_expected_rq_timeout>` that instructs ingress Envoy to respect :ref:`config_http_filters_router_x-envoy-expected-rq-timeout-ms` header, populated by egress Envoy, when deriving timeout for upstream cluster.
* router: added new :ref:`retriable request headers <envoy_api_field_route.Route.per_request_buffer_limit_bytes>` to route configuration, to allow limiting buffering for retries and shadowing.
* router: added new :ref:`retriable request headers <envoy_api_field_route.RetryPolicy.retriable_request_headers>` to retry policies. Retries can now be configured to only trigger on request header match.
* router: added the ability to match a route based on whether a TLS certificate has been
  :ref:`presented <envoy_api_field_route.RouteMatch.TlsContextMatchOptions.presented>` by the
  downstream connection.
* router check tool: add coverage reporting & enforcement.
* router check tool: add comprehensive coverage reporting.
* router check tool: add deprecated field check.
* router check tool: add flag for only printing results of failed tests.
* router check tool: add support for outputting missing tests in the detailed coverage report.
* router check tool: add coverage reporting for direct response routes.
* runtime: allow for the ability to parse boolean values.
* runtime: allow for the ability to parse integers as double values and vice-versa.
* sds: added :ref:`session_ticket_keys_sds_secret_config <envoy_api_field_auth.DownstreamTlsContext.session_ticket_keys_sds_secret_config>` for loading TLS Session Ticket Encryption Keys using SDS API.
* server: added a post initialization lifecycle event, in addition to the existing startup and shutdown events.
* server: added :ref:`per-handler listener stats <config_listener_stats_per_handler>` and
  :ref:`per-worker watchdog stats <operations_performance_watchdog>` to help diagnosing event
  loop imbalance and general performance issues.
* stats: added unit support to histogram.
* tcp_proxy: the default :ref:`idle_timeout
  <envoy_api_field_config.filter.network.tcp_proxy.v2.TcpProxy.idle_timeout>` is now 1 hour.
* thrift_proxy: fix crashing bug on invalid transport/protocol framing
* tls: added verification of IP address SAN fields in certificates against configured SANs in the
* tracing: added support to the Zipkin reporter for sending list of spans as Zipkin JSON v2 and protobuf message over HTTP.
  certificate validation context.
* tracing: added tags for gRPC response status and message.
* tracing: added :ref:`max_path_tag_length <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.tracing>` to support customizing the length of the request path included in the extracted `http.url <https://github.com/opentracing/specification/blob/master/semantic_conventions.md#standard-span-tags-and-log-fields>` tag.
* upstream: added :ref:`an option <envoy_api_field_Cluster.CommonLbConfig.close_connections_on_host_set_change>` that allows draining HTTP, TCP connection pools on cluster membership change.
* upstream: added :ref:`transport_socket_matches <envoy_api_field_Cluster.transport_socket_matches>`, support using different transport socket config when connecting to different upstream endpoints within a cluster.
* upstream: added network filter chains to upstream connections, see :ref:`filters<envoy_api_field_Cluster.filters>`.
* upstream: added new :ref:`failure-percentage based outlier detection<arch_overview_outlier_detection_failure_percentage>` mode.
* upstream: use p2c to select hosts for least-requests load balancers if all host weights are the same, even in cases where weights are not equal to 1.
* upstream: added :ref:`fail_traffic_on_panic <envoy_api_field_Cluster.CommonLbConfig.ZoneAwareLbConfig.fail_traffic_on_panic>` to allow failing all requests to a cluster during panic state.
* zookeeper: parse responses and emit latency stats.

1.11.2 (October 8, 2019)
========================
* http: fixed CVE-2019-15226 by adding a cached byte size in HeaderMap.
* http: added :ref:`max headers count <envoy_api_field_core.HttpProtocolOptions.max_headers_count>` for http connections. The default limit is 100.
* upstream: runtime feature `envoy.reloadable_features.max_response_headers_count` overrides the default limit for upstream :ref:`max headers count <envoy_api_field_Cluster.common_http_protocol_options>`
* http: added :ref:`common_http_protocol_options <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.common_http_protocol_options>`
  Runtime feature `envoy.reloadable_features.max_request_headers_count` overrides the default limit for downstream :ref:`max headers count <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.common_http_protocol_options>`
* regex: backported safe regex matcher fix for CVE-2019-15225.

1.11.1 (August 13, 2019)
========================
* http: added mitigation of client initiated attacks that result in flooding of the downstream HTTP/2 connections. Those attacks can be logged at the "warning" level when the runtime feature `http.connection_manager.log_flood_exception` is enabled. The runtime setting defaults to disabled to avoid log spam when under attack.
* http: added :ref:`inbound_empty_frames_flood <config_http_conn_man_stats_per_codec>` counter stat to the HTTP/2 codec stats, for tracking number of connections terminated for exceeding the limit on consecutive inbound frames with an empty payload and no end stream flag. The limit is configured by setting the :ref:`max_consecutive_inbound_frames_with_empty_payload config setting <envoy_api_field_core.Http2ProtocolOptions.max_consecutive_inbound_frames_with_empty_payload>`.
  Runtime feature `envoy.reloadable_features.http2_protocol_options.max_consecutive_inbound_frames_with_empty_payload` overrides :ref:`max_consecutive_inbound_frames_with_empty_payload setting <envoy_api_field_core.Http2ProtocolOptions.max_consecutive_inbound_frames_with_empty_payload>`. Large override value (i.e. 2147483647) effectively disables mitigation of inbound frames with empty payload.
* http: added :ref:`inbound_priority_frames_flood <config_http_conn_man_stats_per_codec>` counter stat to the HTTP/2 codec stats, for tracking number of connections terminated for exceeding the limit on inbound PRIORITY frames. The limit is configured by setting the :ref:`max_inbound_priority_frames_per_stream config setting <envoy_api_field_core.Http2ProtocolOptions.max_inbound_priority_frames_per_stream>`.
  Runtime feature `envoy.reloadable_features.http2_protocol_options.max_inbound_priority_frames_per_stream` overrides :ref:`max_inbound_priority_frames_per_stream setting <envoy_api_field_core.Http2ProtocolOptions.max_inbound_priority_frames_per_stream>`. Large override value effectively disables flood mitigation of inbound PRIORITY frames.
* http: added :ref:`inbound_window_update_frames_flood <config_http_conn_man_stats_per_codec>` counter stat to the HTTP/2 codec stats, for tracking number of connections terminated for exceeding the limit on inbound WINDOW_UPDATE frames. The limit is configured by setting the :ref:`max_inbound_window_update_frames_per_data_frame_sent config setting <envoy_api_field_core.Http2ProtocolOptions.max_inbound_window_update_frames_per_data_frame_sent>`.
  Runtime feature `envoy.reloadable_features.http2_protocol_options.max_inbound_window_update_frames_per_data_frame_sent` overrides :ref:`max_inbound_window_update_frames_per_data_frame_sent setting <envoy_api_field_core.Http2ProtocolOptions.max_inbound_window_update_frames_per_data_frame_sent>`. Large override value effectively disables flood mitigation of inbound WINDOW_UPDATE frames.
* http: added :ref:`outbound_flood <config_http_conn_man_stats_per_codec>` counter stat to the HTTP/2 codec stats, for tracking number of connections terminated for exceeding the outbound queue limit. The limit is configured by setting the :ref:`max_outbound_frames config setting <envoy_api_field_core.Http2ProtocolOptions.max_outbound_frames>`
  Runtime feature `envoy.reloadable_features.http2_protocol_options.max_outbound_frames` overrides :ref:`max_outbound_frames config setting <envoy_api_field_core.Http2ProtocolOptions.max_outbound_frames>`. Large override value effectively disables flood mitigation of outbound frames of all types.
* http: added :ref:`outbound_control_flood <config_http_conn_man_stats_per_codec>` counter stat to the HTTP/2 codec stats, for tracking number of connections terminated for exceeding the outbound queue limit for PING, SETTINGS and RST_STREAM frames. The limit is configured by setting the :ref:`max_outbound_control_frames config setting <envoy_api_field_core.Http2ProtocolOptions.max_outbound_control_frames>`.
  Runtime feature `envoy.reloadable_features.http2_protocol_options.max_outbound_control_frames` overrides :ref:`max_outbound_control_frames config setting <envoy_api_field_core.Http2ProtocolOptions.max_outbound_control_frames>`. Large override value effectively disables flood mitigation of outbound frames of types PING, SETTINGS and RST_STREAM.
* http: enabled strict validation of HTTP/2 messaging. Previous behavior can be restored using :ref:`stream_error_on_invalid_http_messaging config setting <envoy_api_field_core.Http2ProtocolOptions.stream_error_on_invalid_http_messaging>`.
  Runtime feature `envoy.reloadable_features.http2_protocol_options.stream_error_on_invalid_http_messaging` overrides :ref:`stream_error_on_invalid_http_messaging config setting <envoy_api_field_core.Http2ProtocolOptions.stream_error_on_invalid_http_messaging>`.

1.11.0 (July 11, 2019)
======================
* access log: added a new field for downstream TLS session ID to file and gRPC access logger.
* access log: added a new field for route name to file and gRPC access logger.
* access log: added a new field for response code details in :ref:`file access logger<config_access_log_format_response_code_details>` and :ref:`gRPC access logger<envoy_api_field_data.accesslog.v2.HTTPResponseProperties.response_code_details>`.
* access log: added several new variables for exposing information about the downstream TLS connection to :ref:`file access logger<config_access_log_format_response_code_details>` and :ref:`gRPC access logger<envoy_api_field_data.accesslog.v2.AccessLogCommon.tls_properties>`.
* access log: added a new flag for request rejected due to failed strict header check.
* admin: the administration interface now includes a :ref:`/ready endpoint <operations_admin_interface>` for easier readiness checks.
* admin: extend :ref:`/runtime_modify endpoint <operations_admin_interface_runtime_modify>` to support parameters within the request body.
* admin: the :ref:`/listener endpoint <operations_admin_interface_listeners>` now returns :ref:`listeners.proto<envoy_api_msg_admin.v2alpha.Listeners>` which includes listener names and ports.
* admin: added host priority to :http:get:`/clusters` and :http:get:`/clusters?format=json` endpoint response
* admin: the :ref:`/clusters endpoint <operations_admin_interface_clusters>` now shows hostname
  for each host, useful for DNS based clusters.
* api: track and report requests issued since last load report.
* build: releases are built with Clang and linked with LLD.
* config: added :ref:stats_server_version_override` <envoy_api_field_config.bootstrap.v2.Bootstrap.stats_server_version_override>` in bootstrap, that can be used to override :ref:`server.version statistic <server_statistics>`.
* control-plane: management servers can respond with HTTP 304 to indicate that config is up to date for Envoy proxies polling a :ref:`REST API Config Type <envoy_api_field_core.ApiConfigSource.api_type>`
* csrf: added support for whitelisting additional source origins.
* dns: added support for getting DNS record TTL which is used by STRICT_DNS/LOGICAL_DNS cluster as DNS refresh rate.
* dubbo_proxy: support the :ref:`dubbo proxy filter <config_network_filters_dubbo_proxy>`.
* dynamo_request_parser: adding support for transactions. Adds check for new types of dynamodb operations (TransactWriteItems, TransactGetItems) and awareness for new types of dynamodb errors (IdempotentParameterMismatchException, TransactionCanceledException, TransactionInProgressException).
* eds: added support to specify max time for which endpoints can be used :ref:`gRPC filter <envoy_api_msg_ClusterLoadAssignment.Policy>`.
* eds: removed max limit for `load_balancing_weight`.
* event: added :ref:`loop duration and poll delay statistics <operations_performance>`.
* ext_authz: added a `x-envoy-auth-partial-body` metadata header set to `false|true` indicating if there is a partial body sent in the authorization request message.
* ext_authz: added configurable status code that allows customizing HTTP responses on filter check status errors.
* ext_authz: added option to `ext_authz` that allows the filter clearing route cache.
* grpc-json: added support for :ref:`auto mapping
  <envoy_api_field_config.filter.http.transcoder.v2.GrpcJsonTranscoder.auto_mapping>`.
* health check: added :ref:`initial jitter <envoy_api_field_core.HealthCheck.initial_jitter>` to add jitter to the first health check in order to prevent thundering herd on Envoy startup.
* hot restart: stats are no longer shared between hot restart parent/child via shared memory, but rather by RPC. Hot restart version incremented to 11.
* http: added the ability to pass a URL encoded PEM encoded peer certificate chain in the
  :ref:`config_http_conn_man_headers_x-forwarded-client-cert` header.
* http: fixed a bug where large unbufferable responses were not tracked in stats and logs correctly.
* http: fixed a crashing bug where gRPC local replies would cause segfaults when upstream access logging was on.
* http: mitigated a race condition with the :ref:`delayed_close_timeout<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.delayed_close_timeout>` where it could trigger while actively flushing a pending write buffer for a downstream connection.
* http: added support for :ref:`preserve_external_request_id<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.preserve_external_request_id>` that represents whether the x-request-id should not be reset on edge entry inside mesh
* http: changed `sendLocalReply` to send percent-encoded `GrpcMessage`.
* http: added a :ref:header_prefix` <envoy_api_field_config.bootstrap.v2.Bootstrap.header_prefix>` configuration option to allow Envoy to send and process x-custom- prefixed headers rather than x-envoy.
* http: added :ref:`dynamic forward proxy <arch_overview_http_dynamic_forward_proxy>` support.
* http: tracking the active stream and dumping state in Envoy crash handlers. This can be disabled by building with `--define disable_object_dump_on_signal_trace=disabled`
* jwt_authn: make filter's parsing of JWT more flexible, allowing syntax like ``jwt=eyJhbGciOiJS...ZFnFIw,extra=7,realm=123``
* listener: added :ref:`source IP <envoy_api_field_listener.FilterChainMatch.source_prefix_ranges>`
  and :ref:`source port <envoy_api_field_listener.FilterChainMatch.source_ports>` filter
  chain matching.
* lua: exposed functions to Lua to verify digital signature.
* original_src filter: added the :ref:`filter<config_http_filters_original_src>`.
* outlier_detector: added configuration :ref:`outlier_detection.split_external_local_origin_errors<envoy_api_field_cluster.OutlierDetection.split_external_local_origin_errors>` to distinguish locally and externally generated errors. See :ref:`arch_overview_outlier_detection` for full details.
* rbac: migrated from v2alpha to v2.
* redis: add support for Redis cluster custom cluster type.
* redis: automatically route commands using cluster slots for Redis cluster.
* redis: added :ref:`prefix routing <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.prefix_routes>` to enable routing commands based on their key's prefix to different upstream.
* redis: added :ref:`request mirror policy <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.PrefixRoutes.Route.request_mirror_policy>` to enable shadow traffic and/or dual writes.
* redis: add support for zpopmax and zpopmin commands.
* redis: added
  :ref:`max_buffer_size_before_flush <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.ConnPoolSettings.max_buffer_size_before_flush>` to batch commands together until the encoder buffer hits a certain size, and
  :ref:`buffer_flush_timeout <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.ConnPoolSettings.buffer_flush_timeout>` to control how quickly the buffer is flushed if it is not full.
* redis: added auth support :ref:`downstream_auth_password <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.downstream_auth_password>` for downstream client authentication, and :ref:`auth_password <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProtocolOptions.auth_password>` to configure authentication passwords for upstream server clusters.
* retry: added a retry predicate that :ref:`rejects canary hosts. <envoy_api_field_route.RetryPolicy.retry_host_predicate>`
* router: add support for configuring a :ref:`gRPC timeout offset <envoy_api_field_route.RouteAction.grpc_timeout_offset>` on incoming requests.
* router: added ability to control retry back-off intervals via :ref:`retry policy <envoy_api_msg_route.RetryPolicy.RetryBackOff>`.
* router: added ability to issue a hedged retry in response to a per try timeout via a :ref:`hedge policy <envoy_api_msg_route.HedgePolicy>`.
* router: added a route name field to each http route in route.Route list
* router: added several new variables for exposing information about the downstream TLS connection via :ref:`header
  formatters <config_http_conn_man_headers_custom_request_headers>`.
* router: per try timeouts will no longer start before the downstream request has been received in full by the router.This ensures that the per try timeout does not account for slow downstreams and that will not start before the global timeout.
* router: added :ref:`RouteAction's auto_host_rewrite_header <envoy_api_field_route.RouteAction.auto_host_rewrite_header>` to allow upstream host header substitution with some other header's value
* router: added support for UPSTREAM_REMOTE_ADDRESS :ref:`header formatter
  <config_http_conn_man_headers_custom_request_headers>`.
* router: add ability to reject a request that includes invalid values for
  headers configured in :ref:`strict_check_headers <envoy_api_field_config.filter.http.router.v2.Router.strict_check_headers>`
* runtime: added support for :ref:`flexible layering configuration
  <envoy_api_field_config.bootstrap.v2.Bootstrap.layered_runtime>`.
* runtime: added support for statically :ref:`specifying the runtime in the bootstrap configuration
  <envoy_api_field_config.bootstrap.v2.Runtime.base>`.
* runtime: :ref:`Runtime Discovery Service (RTDS) <config_runtime_rtds>` support added to layered runtime configuration.
* sandbox: added :ref:`CSRF sandbox <install_sandboxes_csrf>`.
* server: ``--define manual_stamp=manual_stamp`` was added to allow server stamping outside of binary rules.
  more info in the `bazel docs <https://github.com/envoyproxy/envoy/blob/master/bazel/README.md#enabling-optional-features>`_.
* server: added :ref:`server state <statistics>` statistic.
* server: added :ref:`initialization_time_ms<statistics>` statistic.
* subset: added :ref:`list_as_any<envoy_api_field_Cluster.LbSubsetConfig.list_as_any>` option to
  the subset lb which allows matching metadata against any of the values in a list value
  on the endpoints.
* tools: added :repo:`proto <test/tools/router_check/validation.proto>` support for :ref:`router check tool <install_tools_route_table_check_tool>` tests.
* tracing: add trace sampling configuration to the route, to override the route level.
* upstream: added :ref:`upstream_cx_pool_overflow <config_cluster_manager_cluster_stats>` for the connection pool circuit breaker.
* upstream: an EDS management server can now force removal of a host that is still passing active
  health checking by first marking the host as failed via EDS health check and subsequently removing
  it in a future update. This is a mechanism to work around a race condition in which an EDS
  implementation may remove a host before it has stopped passing active HC, thus causing the host
  to become stranded until a future update.
* upstream: added :ref:`an option <envoy_api_field_Cluster.CommonLbConfig.ignore_new_hosts_until_first_hc>`
  that allows ignoring new hosts for the purpose of load balancing calculations until they have
  been health checked for the first time.
* upstream: added runtime error checking to prevent setting dns type to STRICT_DNS or LOGICAL_DNS when custom resolver name is specified.
* upstream: added possibility to override fallback_policy per specific selector in :ref:`subset load balancer <arch_overview_load_balancer_subsets>`.
* upstream: the :ref:`logical DNS cluster <arch_overview_service_discovery_types_logical_dns>` now
  displays the current resolved IP address in admin output instead of 0.0.0.0.

1.10.0 (Apr 5, 2019)
====================
* access log: added a new flag for upstream retry count exceeded.
* access log: added a :ref:`gRPC filter <envoy_api_msg_config.filter.accesslog.v2.GrpcStatusFilter>` to allow filtering on gRPC status.
* access log: added a new flag for stream idle timeout.
* access log: added a new field for upstream transport failure reason in :ref:`file access logger<config_access_log_format_upstream_transport_failure_reason>` and
  :ref:`gRPC access logger<envoy_api_field_data.accesslog.v2.AccessLogCommon.upstream_transport_failure_reason>` for HTTP access logs.
* access log: added new fields for downstream x509 information (URI sans and subject) to file and gRPC access logger.
* admin: the admin server can now be accessed via HTTP/2 (prior knowledge).
* admin: changed HTTP response status code from 400 to 405 when attempting to GET a POST-only route (such as /quitquitquit).
* buffer: fix vulnerabilities when allocation fails.
* build: releases are built with GCC-7 and linked with LLD.
* build: dev docker images :ref:`have been split <install_binaries>` from tagged images for easier
  discoverability in Docker Hub. Additionally, we now build images for point releases.
* config: added support of using google.protobuf.Any in opaque configs for extensions.
* config: logging warnings when deprecated fields are in use.
* config: removed deprecated --v2-config-only from command line config.
* config: removed deprecated_v1 sds_config from :ref:`Bootstrap config <config_overview_v2_bootstrap>`.
* config: removed the deprecated_v1 config option from :ref:`ring hash <envoy_api_msg_Cluster.RingHashLbConfig>`.
* config: removed REST_LEGACY as a valid :ref:`ApiType <envoy_api_field_core.ApiConfigSource.api_type>`.
* config: finish cluster warming only when a named response i.e. ClusterLoadAssignment associated to the cluster being warmed comes in the EDS response. This is a behavioural change from the current implementation where warming of cluster completes on missing load assignments also.
* config: use Envoy cpuset size to set the default number or worker threads if :option:`--cpuset-threads` is enabled.
* config: added support for :ref:`initial_fetch_timeout <envoy_api_field_core.ConfigSource.initial_fetch_timeout>`. The timeout is disabled by default.
* cors: added :ref:`filter_enabled & shadow_enabled RuntimeFractionalPercent flags <cors-runtime>` to filter.
* csrf: added :ref:`CSRF filter <config_http_filters_csrf>`.
* ext_authz: added support for buffering request body.
* ext_authz: migrated from v2alpha to v2 and improved docs.
* ext_authz: added a configurable option to make the gRPC service cross-compatible with V2Alpha. Note that this feature is already deprecated. It should be used for a short time, and only when transitioning from alpha to V2 release version.
* ext_authz: migrated from v2alpha to v2 and improved the documentation.
* ext_authz: authorization request and response configuration has been separated into two distinct objects: :ref:`authorization request
  <envoy_api_field_config.filter.http.ext_authz.v2.HttpService.authorization_request>` and :ref:`authorization response
  <envoy_api_field_config.filter.http.ext_authz.v2.HttpService.authorization_response>`. In addition, :ref:`client headers
  <envoy_api_field_config.filter.http.ext_authz.v2.AuthorizationResponse.allowed_client_headers>` and :ref:`upstream headers
  <envoy_api_field_config.filter.http.ext_authz.v2.AuthorizationResponse.allowed_upstream_headers>` replaces the previous *allowed_authorization_headers* object.
  All the control header lists now support :ref:`string matcher <envoy_api_msg_type.matcher.StringMatcher>` instead of standard string.
* fault: added the :ref:`max_active_faults
  <envoy_api_field_config.filter.http.fault.v2.HTTPFault.max_active_faults>` setting, as well as
  :ref:`statistics <config_http_filters_fault_injection_stats>` for the number of active faults
  and the number of faults the overflowed.
* fault: added :ref:`response rate limit
  <envoy_api_field_config.filter.http.fault.v2.HTTPFault.response_rate_limit>` fault injection.
* fault: added :ref:`HTTP header fault configuration
  <config_http_filters_fault_injection_http_header>` to the HTTP fault filter.
* governance: extending Envoy deprecation policy from 1 release (0-3 months) to 2 releases (3-6 months).
* health check: expected response codes in http health checks are now :ref:`configurable <envoy_api_msg_core.HealthCheck.HttpHealthCheck>`.
* http: added new grpc_http1_reverse_bridge filter for converting gRPC requests into HTTP/1.1 requests.
* http: fixed a bug where Content-Length:0 was added to HTTP/1 204 responses.
* http: added :ref:`max request headers size <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.max_request_headers_kb>`. The default behaviour is unchanged.
* http: added modifyDecodingBuffer/modifyEncodingBuffer to allow modifying the buffered request/response data.
* http: added encodeComplete/decodeComplete. These are invoked at the end of the stream, after all data has been encoded/decoded respectively. Default implementation is a no-op.
* outlier_detection: added support for :ref:`outlier detection event protobuf-based logging <arch_overview_outlier_detection_logging>`.
* mysql: added a MySQL proxy filter that is capable of parsing SQL queries over MySQL wire protocol. Refer to :ref:`MySQL proxy<config_network_filters_mysql_proxy>` for more details.
* performance: new buffer implementation (disabled by default; to test it, add "--use-libevent-buffers 0" to the command-line arguments when starting Envoy).
* jwt_authn: added :ref:`filter_state_rules <envoy_api_field_config.filter.http.jwt_authn.v2alpha.JwtAuthentication.filter_state_rules>` to allow specifying requirements from filterState by other filters.
* ratelimit: removed deprecated rate limit configuration from bootstrap.
* redis: added :ref:`hashtagging <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.ConnPoolSettings.enable_hashtagging>` to guarantee a given key's upstream.
* redis: added :ref:`latency stats <config_network_filters_redis_proxy_per_command_stats>` for commands.
* redis: added :ref:`success and error stats <config_network_filters_redis_proxy_per_command_stats>` for commands.
* redis: migrate hash function for host selection to `MurmurHash2 <https://sites.google.com/site/murmurhash>`_ from std::hash. MurmurHash2 is compatible with std::hash in GNU libstdc++ 3.4.20 or above. This is typically the case when compiled on Linux and not macOS.
* redis: added :ref:`latency_in_micros <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.latency_in_micros>` to specify the redis commands stats time unit in microseconds.
* router: added ability to configure a :ref:`retry policy <envoy_api_msg_route.RetryPolicy>` at the
  virtual host level.
* router: added reset reason to response body when upstream reset happens. After this change, the response body will be of the form `upstream connect error or disconnect/reset before headers. reset reason:`
* router: added :ref:`rq_reset_after_downstream_response_started <config_http_filters_router_stats>` counter stat to router stats.
* router: added per-route configuration of :ref:`internal redirects <envoy_api_field_route.RouteAction.internal_redirect_action>`.
* router: removed deprecated route-action level headers_to_add/remove.
* router: made :ref:`max retries header <config_http_filters_router_x-envoy-max-retries>` take precedence over the number of retries in route and virtual host retry policies.
* router: added support for prefix wildcards in :ref:`virtual host domains<envoy_api_field_route.VirtualHost.domains>`
* stats: added support for histograms in prometheus
* stats: added usedonly flag to prometheus stats to only output metrics which have been
  updated at least once.
* stats: added gauges tracking remaining resources before circuit breakers open.
* tap: added new alpha :ref:`HTTP tap filter <config_http_filters_tap>`.
* tls: enabled TLS 1.3 on the server-side (non-FIPS builds).
* upstream: add hash_function to specify the hash function for :ref:`ring hash<envoy_api_msg_Cluster.RingHashLbConfig>` as either xxHash or `murmurHash2 <https://sites.google.com/site/murmurhash>`_. MurmurHash2 is compatible with std::hash in GNU libstdc++ 3.4.20 or above. This is typically the case when compiled on Linux and not macOS.
* upstream: added :ref:`degraded health value<arch_overview_load_balancing_degraded>` which allows
  routing to certain hosts only when there are insufficient healthy hosts available.
* upstream: add cluster factory to allow creating and registering :ref:`custom cluster type<arch_overview_service_discovery_types_custom>`.
* upstream: added a :ref:`circuit breaker <arch_overview_circuit_break_cluster_maximum_connection_pools>` to limit the number of concurrent connection pools in use.
* tracing: added :ref:`verbose <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.tracing>` to support logging annotations on spans.
* upstream: added support for host weighting and :ref:`locality weighting <arch_overview_load_balancing_locality_weighted_lb>` in the :ref:`ring hash load balancer <arch_overview_load_balancing_types_ring_hash>`, and added a :ref:`maximum_ring_size<envoy_api_field_Cluster.RingHashLbConfig.maximum_ring_size>` config parameter to strictly bound the ring size.
* zookeeper: added a ZooKeeper proxy filter that parses ZooKeeper messages (requests/responses/events).
  Refer to :ref:`ZooKeeper proxy<config_network_filters_zookeeper_proxy>` for more details.
* upstream: added configuration option to select any host when the fallback policy fails.
* upstream: stopped incrementing upstream_rq_total for HTTP/1 conn pool when request is circuit broken.

1.9.1 (Apr 2, 2019)
===================
* http: fixed CVE-2019-9900 by rejecting HTTP/1.x headers with embedded NUL characters.
* http: fixed CVE-2019-9901 by normalizing HTTP paths prior to routing or L7 data plane processing.
  This defaults off and is configurable via either HTTP connection manager :ref:`normalize_path
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.normalize_path>`
  or the :ref:`runtime <config_http_conn_man_runtime_normalize_path>`.

1.9.0 (Dec 20, 2018)
====================
* access log: added a :ref:`JSON logging mode <config_access_log_format_dictionaries>` to output access logs in JSON format.
* access log: added dynamic metadata to access log messages streamed over gRPC.
* access log: added DOWNSTREAM_CONNECTION_TERMINATION.
* admin: :http:post:`/logging` now responds with 200 while there are no params.
* admin: added support for displaying subject alternate names in :ref:`certs<operations_admin_interface_certs>` end point.
* admin: added host weight to the :http:get:`/clusters?format=json` end point response.
* admin: :http:get:`/server_info` now responds with a JSON object instead of a single string.
* admin: :http:get:`/server_info` now exposes what stage of initialization the server is currently in.
* admin: added support for displaying command line options in :http:get:`/server_info` end point.
* circuit-breaker: added cx_open, rq_pending_open, rq_open and rq_retry_open gauges to expose live
  state via :ref:`circuit breakers statistics <config_cluster_manager_cluster_stats_circuit_breakers>`.
* cluster: set a default of 1s for :ref:`option <envoy_api_field_Cluster.CommonLbConfig.update_merge_window>`.
* config: removed support for the v1 API.
* config: added support for :ref:`rate limiting<envoy_api_msg_core.RateLimitSettings>` discovery request calls.
* cors: added :ref:`invalid/valid stats <cors-statistics>` to filter.
* ext-authz: added support for providing per route config - optionally disable the filter and provide context extensions.
* fault: removed integer percentage support.
* grpc-json: added support for :ref:`ignoring query parameters
  <envoy_api_field_config.filter.http.transcoder.v2.GrpcJsonTranscoder.ignored_query_parameters>`.
* health check: added :ref:`logging health check failure events <envoy_api_field_core.HealthCheck.always_log_health_check_failures>`.
* health check: added ability to set :ref:`authority header value
  <envoy_api_field_core.HealthCheck.GrpcHealthCheck.authority>` for gRPC health check.
* http: added HTTP/2 WebSocket proxying via :ref:`extended CONNECT <envoy_api_field_core.Http2ProtocolOptions.allow_connect>`.
* http: added limits to the number and length of header modifications in all fields request_headers_to_add and response_headers_to_add. These limits are very high and should only be used as a last-resort safeguard.
* http: added support for a :ref:`request timeout <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.request_timeout>`. The timeout is disabled by default.
* http: no longer adding whitespace when appending X-Forwarded-For headers. **Warning**: this is not
  compatible with 1.7.0 builds prior to `9d3a4eb4ac44be9f0651fcc7f87ad98c538b01ee <https://github.com/envoyproxy/envoy/pull/3610>`_.
  See `#3611 <https://github.com/envoyproxy/envoy/issues/3611>`_ for details.
* http: augmented the `sendLocalReply` filter API to accept an optional `GrpcStatus`
  value to override the default HTTP to gRPC status mapping.
* http: no longer close the TCP connection when a HTTP/1 request is retried due
  to a response with empty body.
* http: added support for more gRPC content-type headers in :ref:`gRPC bridge filter <config_http_filters_grpc_bridge>`, like application/grpc+proto.
* listeners: all listener filters are now governed by the :ref:`listener_filters_timeout
  <envoy_api_field_Listener.listener_filters_timeout>` setting. The hard coded 15s timeout in
  the :ref:`TLS inspector listener filter <config_listener_filters_tls_inspector>` is superseded by
  this setting.
* listeners: added the ability to match :ref:`FilterChain <envoy_api_msg_listener.FilterChain>` using :ref:`source_type <envoy_api_field_listener.FilterChainMatch.source_type>`.
* load balancer: added a `configuration <envoy_api_msg_Cluster.LeastRequestLbConfig>` option to specify the number of choices made in P2C.
* logging: added missing [ in log prefix.
* mongo_proxy: added :ref:`dynamic metadata <config_network_filters_mongo_proxy_dynamic_metadata>`.
* network: removed the reference to `FilterState` in `Connection` in favor of `StreamInfo`.
* rate-limit: added :ref:`configuration <envoy_api_field_config.filter.http.rate_limit.v2.RateLimit.rate_limited_as_resource_exhausted>`
  to specify whether the `GrpcStatus` status returned should be `RESOURCE_EXHAUSTED` or
  `UNAVAILABLE` when a gRPC call is rate limited.
* rate-limit: removed support for the legacy ratelimit service and made the data-plane-api
  :ref:`rls.proto <envoy_api_file_envoy/service/ratelimit/v2/rls.proto>` based implementation default.
* rate-limit: removed the deprecated cluster_name attribute in :ref:`rate limit service configuration <envoy_api_file_envoy/config/ratelimit/v2/rls.proto>`.
* rate-limit: added :ref:`rate_limit_service <envoy_api_msg_config.filter.http.rate_limit.v2.RateLimit>` configuration to filters.
* rbac: added dynamic metadata to the network level filter.
* rbac: added support for permission matching by :ref:`requested server name <envoy_api_field_config.rbac.v2.Permission.requested_server_name>`.
* redis: static cluster configuration is no longer required. Redis proxy will work with clusters
  delivered via CDS.
* router: added ability to configure arbitrary :ref:`retriable status codes. <envoy_api_field_route.RetryPolicy.retriable_status_codes>`
* router: added ability to set attempt count in upstream requests, see :ref:`virtual host's include request
  attempt count flag <envoy_api_field_route.VirtualHost.include_request_attempt_count>`.
* router: added internal :ref:`grpc-retry-on <config_http_filters_router_x-envoy-retry-grpc-on>` policy.
* router: added :ref:`scheme_redirect <envoy_api_field_route.RedirectAction.scheme_redirect>` and
  :ref:`port_redirect <envoy_api_field_route.RedirectAction.port_redirect>` to define the respective
  scheme and port rewriting RedirectAction.
* router: when :ref:`max_grpc_timeout <envoy_api_field_route.RouteAction.max_grpc_timeout>`
  is set, Envoy will now add or update the grpc-timeout header to reflect Envoy's expected timeout.
* router: per try timeouts now starts when an upstream stream is ready instead of when the request has
  been fully decoded by Envoy.
* router: added support for not retrying :ref:`rate limited requests<config_http_filters_router_x-envoy-ratelimited>`. Rate limit filter now sets the :ref:`x-envoy-ratelimited<config_http_filters_router_x-envoy-ratelimited>`
  header so the rate limited requests that may have been retried earlier will not be retried with this change.
* router: added support for enabling upgrades on a :ref:`per-route <envoy_api_field_route.RouteAction.upgrade_configs>` basis.
* router: support configuring a default fraction of mirror traffic via
  :ref:`runtime_fraction <envoy_api_field_route.RouteAction.RequestMirrorPolicy.runtime_key>`.
* sandbox: added :ref:`cors sandbox <install_sandboxes_cors>`.
* server: added `SIGINT` (Ctrl-C) handler to gracefully shutdown Envoy like `SIGTERM`.
* stats: added :ref:`stats_matcher <envoy_api_field_config.metrics.v2.StatsConfig.stats_matcher>` to the bootstrap config for granular control of stat instantiation.
* stream: renamed the `RequestInfo` namespace to `StreamInfo` to better match
  its behaviour within TCP and HTTP implementations.
* stream: renamed `perRequestState` to `filterState` in `StreamInfo`.
* stream: added `downstreamDirectRemoteAddress` to `StreamInfo`.
* thrift_proxy: introduced thrift rate limiter filter.
* tls: added ssl.curves.<curve>, ssl.sigalgs.<sigalg> and ssl.versions.<version> to
  :ref:`listener metrics <config_listener_stats>` to track TLS algorithms and versions in use.
* tls: added support for :ref:`client-side session resumption <envoy_api_field_auth.UpstreamTlsContext.max_session_keys>`.
* tls: added support for CRLs in :ref:`trusted_ca <envoy_api_field_auth.CertificateValidationContext.trusted_ca>`.
* tls: added support for :ref:`multiple server TLS certificates <arch_overview_ssl_cert_select>`.
* tls: added support for :ref:`password encrypted private keys <envoy_api_field_auth.TlsCertificate.password>`.
* tls: added the ability to build :ref:`BoringSSL FIPS <arch_overview_ssl_fips>` using ``--define boringssl=fips`` Bazel option.
* tls: removed support for ECDSA certificates with curves other than P-256.
* tls: removed support for RSA certificates with keys smaller than 2048-bits.
* tracing: added support to the Zipkin tracer for the :ref:`b3 <config_http_conn_man_headers_b3>` single header format.
* tracing: added support for :ref:`Datadog <arch_overview_tracing>` tracer.
* upstream: added :ref:`scale_locality_weight<envoy_api_field_Cluster.LbSubsetConfig.scale_locality_weight>` to enable
  scaling locality weights by number of hosts removed by subset lb predicates.
* upstream: changed how load calculation for :ref:`priority levels<arch_overview_load_balancing_priority_levels>` and :ref:`panic thresholds<arch_overview_load_balancing_panic_threshold>` interact. As long as normalized total health is 100% panic thresholds are disregarded.
* upstream: changed the default hash for :ref:`ring hash <envoy_api_msg_Cluster.RingHashLbConfig>` from std::hash to `xxHash <https://github.com/Cyan4973/xxHash>`_.
* upstream: when using active health checking and STRICT_DNS with several addresses that resolve
  to the same hosts, Envoy will now health check each host independently.

1.8.0 (Oct 4, 2018)
===================
* access log: added :ref:`response flag filter <envoy_api_msg_config.filter.accesslog.v2.ResponseFlagFilter>`
  to filter based on the presence of Envoy response flags.
* access log: added RESPONSE_DURATION and RESPONSE_TX_DURATION.
* access log: added REQUESTED_SERVER_NAME for SNI to tcp_proxy and http
* admin: added :http:get:`/hystrix_event_stream` as an endpoint for monitoring envoy's statistics
  through `Hystrix dashboard <https://github.com/Netflix-Skunkworks/hystrix-dashboard/wiki>`_.
* cli: added support for :ref:`component log level <operations_cli>` command line option for configuring log levels of individual components.
* cluster: added :ref:`option <envoy_api_field_Cluster.CommonLbConfig.update_merge_window>` to merge
  health check/weight/metadata updates within the given duration.
* config: regex validation added to limit to a maximum of 1024 characters.
* config: v1 disabled by default. v1 support remains available until October via flipping --v2-config-only=false.
* config: v1 disabled by default. v1 support remains available until October via deprecated flag --allow-deprecated-v1-api.
* config: fixed stat inconsistency between xDS and ADS implementation. :ref:`update_failure <config_cluster_manager_cds>`
  stat is incremented in case of network failure and :ref:`update_rejected <config_cluster_manager_cds>` stat is incremented
  in case of schema/validation error.
* config: added a stat :ref:`connected_state <management_server_stats>` that indicates current connected state of Envoy with
  management server.
* ext_authz: added support for configuring additional :ref:`authorization headers <envoy_api_field_config.filter.http.ext_authz.v2.AuthorizationRequest.headers_to_add>`
  to be sent from Envoy to the authorization service.
* fault: added support for fractional percentages in :ref:`FaultDelay <envoy_api_field_config.filter.fault.v2.FaultDelay.percentage>`
  and in :ref:`FaultAbort <envoy_api_field_config.filter.http.fault.v2.FaultAbort.percentage>`.
* grpc-json: added support for building HTTP response from
  `google.api.HttpBody <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto>`_.
* health check: added support for :ref:`custom health check <envoy_api_field_core.HealthCheck.custom_health_check>`.
* health check: added support for :ref:`specifying jitter as a percentage <envoy_api_field_core.HealthCheck.interval_jitter_percent>`.
* health_check: added support for :ref:`health check event logging <arch_overview_health_check_logging>`.
* health_check: added :ref:`timestamp <envoy_api_field_data.core.v2alpha.HealthCheckEvent.timestamp>`
  to the :ref:`health check event <envoy_api_msg_data.core.v2alpha.HealthCheckEvent>` definition.
* health_check: added support for specifying :ref:`custom request headers <config_http_conn_man_headers_custom_request_headers>`
  to HTTP health checker requests.
* http: added support for a :ref:`per-stream idle timeout
  <envoy_api_field_route.RouteAction.idle_timeout>`. This applies at both :ref:`connection manager
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.stream_idle_timeout>`
  and :ref:`per-route granularity <envoy_api_field_route.RouteAction.idle_timeout>`. The timeout
  defaults to 5 minutes; if you have other timeouts (e.g. connection idle timeout, upstream
  response per-retry) that are longer than this in duration, you may want to consider setting a
  non-default per-stream idle timeout.
* http: added upstream_rq_completed counter for :ref:`total requests completed <config_cluster_manager_cluster_stats_dynamic_http>` to dynamic HTTP counters.
* http: added downstream_rq_completed counter for :ref:`total requests completed <config_http_conn_man_stats>`, including on a :ref:`per-listener basis <config_http_conn_man_stats_per_listener>`.
* http: added generic :ref:`Upgrade support
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.upgrade_configs>`.
* http: better handling of HEAD requests. Now sending transfer-encoding: chunked rather than content-length: 0.
* http: fixed missing support for appending to predefined inline headers, e.g.
  *authorization*, in features that interact with request and response headers,
  e.g. :ref:`request_headers_to_add
  <envoy_api_field_route.Route.request_headers_to_add>`. For example, a
  request header *authorization: token1* will appear as *authorization:
  token1,token2*, after having :ref:`request_headers_to_add
  <envoy_api_field_route.Route.request_headers_to_add>` with *authorization:
  token2* applied.
* http: response filters not applied to early error paths such as http_parser generated 400s.
* http: restrictions added to reject *:*-prefixed pseudo-headers in :ref:`custom
  request headers <config_http_conn_man_headers_custom_request_headers>`.
* http: :ref:`hpack_table_size <envoy_api_field_core.Http2ProtocolOptions.hpack_table_size>` now controls
  dynamic table size of both: encoder and decoder.
* http: added support for removing request headers using :ref:`request_headers_to_remove
  <envoy_api_field_route.Route.request_headers_to_remove>`.
* http: added support for a :ref:`delayed close timeout<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.delayed_close_timeout>` to mitigate race conditions when closing connections to downstream HTTP clients. The timeout defaults to 1 second.
* jwt-authn filter: add support for per route JWT requirements.
* listeners: added the ability to match :ref:`FilterChain <envoy_api_msg_listener.FilterChain>` using
  :ref:`destination_port <envoy_api_field_listener.FilterChainMatch.destination_port>` and
  :ref:`prefix_ranges <envoy_api_field_listener.FilterChainMatch.prefix_ranges>`.
* lua: added :ref:`connection() <config_http_filters_lua_connection_wrapper>` wrapper and *ssl()* API.
* lua: added :ref:`streamInfo() <config_http_filters_lua_stream_info_wrapper>` wrapper and *protocol()* API.
* lua: added :ref:`streamInfo():dynamicMetadata() <config_http_filters_lua_stream_info_dynamic_metadata_wrapper>` API.
* network: introduced :ref:`sni_cluster <config_network_filters_sni_cluster>` network filter that forwards connections to the
  upstream cluster specified by the SNI value presented by the client during a TLS handshake.
* proxy_protocol: added support for HAProxy Proxy Protocol v2 (AF_INET/AF_INET6 only).
* ratelimit: added support for :repo:`api/envoy/service/ratelimit/v2/rls.proto`.
  Lyft's reference implementation of the `ratelimit <https://github.com/lyft/ratelimit>`_ service also supports the data-plane-api proto as of v1.1.0.
  Envoy can use either proto to send client requests to a ratelimit server with the use of the
  `use_data_plane_proto` boolean flag in the ratelimit configuration.
  Support for the legacy proto `source/common/ratelimit/ratelimit.proto` is deprecated and will be removed at the start of the 1.9.0 release cycle.
* ratelimit: added :ref:`failure_mode_deny <envoy_api_msg_config.filter.http.rate_limit.v2.RateLimit>` option to control traffic flow in
  case of rate limit service error.
* rbac config: added a :ref:`principal_name <envoy_api_field_config.rbac.v2.Principal.Authenticated.principal_name>` field and
  removed the old `name` field to give more flexibility for matching certificate identity.
* rbac network filter: a :ref:`role-based access control network filter <config_network_filters_rbac>` has been added.
* rest-api: added ability to set the :ref:`request timeout <envoy_api_field_core.ApiConfigSource.request_timeout>` for REST API requests.
* route checker: added v2 config support and removed support for v1 configs.
* router: added ability to set request/response headers at the :ref:`envoy_api_msg_route.Route` level.
* stats: added :ref:`option to configure the DogStatsD metric name prefix<envoy_api_field_config.metrics.v2.DogStatsdSink.prefix>` to DogStatsdSink.
* tcp_proxy: added support for :ref:`weighted clusters <envoy_api_field_config.filter.network.tcp_proxy.v2.TcpProxy.weighted_clusters>`.
* thrift_proxy: introduced thrift routing, moved configuration to correct location
* thrift_proxy: introduced thrift configurable decoder filters
* tls: implemented :ref:`Secret Discovery Service <config_secret_discovery_service>`.
* tracing: added support for configuration of :ref:`tracing sampling
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.tracing>`.
* upstream: added configuration option to the subset load balancer to take locality weights into account when
  selecting a host from a subset.
* upstream: require opt-in to use the :ref:`x-envoy-original-dst-host <config_http_conn_man_headers_x-envoy-original-dst-host>` header
  for overriding destination address when using the :ref:`Original Destination <arch_overview_load_balancing_types_original_destination>`
  load balancing policy.

1.7.0 (Jun 21, 2018)
====================
* access log: added ability to log response trailers.
* access log: added ability to format START_TIME.
* access log: added DYNAMIC_METADATA :ref:`access log formatter <config_access_log_format>`.
* access log: added :ref:`HeaderFilter <envoy_api_msg_config.filter.accesslog.v2.HeaderFilter>`
  to filter logs based on request headers.
* access log: added `%([1-9])?f` as one of START_TIME specifiers to render subseconds.
* access log: gRPC Access Log Service (ALS) support added for :ref:`HTTP access logs
  <envoy_api_msg_config.accesslog.v2.HttpGrpcAccessLogConfig>`.
* access log: improved WebSocket logging.
* admin: added :http:get:`/config_dump` for dumping the current configuration and associated xDS
  version information (if applicable).
* admin: added :http:get:`/clusters?format=json` for outputing a JSON-serialized proto detailing
  the current status of all clusters.
* admin: added :http:get:`/stats/prometheus` as an alternative endpoint for getting stats in prometheus format.
* admin: added :ref:`/runtime_modify endpoint <operations_admin_interface_runtime_modify>` to add or change runtime values.
* admin: mutations must be sent as POSTs, rather than GETs. Mutations include:
  :http:post:`/cpuprofiler`, :http:post:`/healthcheck/fail`, :http:post:`/healthcheck/ok`,
  :http:post:`/logging`, :http:post:`/quitquitquit`, :http:post:`/reset_counters`,
  :http:post:`/runtime_modify?key1=value1&key2=value2&keyN=valueN`.
* admin: removed `/routes` endpoint; route configs can now be found at the :ref:`/config_dump endpoint <operations_admin_interface_config_dump>`.
* buffer filter: the buffer filter can be optionally
  :ref:`disabled <envoy_api_field_config.filter.http.buffer.v2.BufferPerRoute.disabled>` or
  :ref:`overridden <envoy_api_field_config.filter.http.buffer.v2.BufferPerRoute.buffer>` with
  route-local configuration.
* cli: added --config-yaml flag to the Envoy binary. When set its value is interpreted as a yaml
  representation of the bootstrap config and overrides --config-path.
* cluster: added :ref:`option <envoy_api_field_Cluster.close_connections_on_host_health_failure>`
  to close tcp_proxy upstream connections when health checks fail.
* cluster: added :ref:`option <envoy_api_field_Cluster.drain_connections_on_host_removal>` to drain
  connections from hosts after they are removed from service discovery, regardless of health status.
* cluster: fixed bug preventing the deletion of all endpoints in a priority
* debug: added symbolized stack traces (where supported)
* ext-authz filter: added support to raw HTTP authorization.
* ext-authz filter: added support to gRPC responses to carry HTTP attributes.
* grpc: support added for the full set of :ref:`Google gRPC call credentials
  <envoy_api_msg_core.GrpcService.GoogleGrpc.CallCredentials>`.
* gzip filter: added :ref:`stats <gzip-statistics>` to the filter.
* gzip filter: sending *accept-encoding* header as *identity* no longer compresses the payload.
* health check: added ability to set :ref:`additional HTTP headers
  <envoy_api_field_core.HealthCheck.HttpHealthCheck.request_headers_to_add>` for HTTP health check.
* health check: added support for EDS delivered :ref:`endpoint health status
  <envoy_api_field_endpoint.LbEndpoint.health_status>`.
* health check: added interval overrides for health state transitions from :ref:`healthy to unhealthy
  <envoy_api_field_core.HealthCheck.unhealthy_edge_interval>`, :ref:`unhealthy to healthy
  <envoy_api_field_core.HealthCheck.healthy_edge_interval>` and for subsequent checks on
  :ref:`unhealthy hosts <envoy_api_field_core.HealthCheck.unhealthy_interval>`.
* health check: added support for :ref:`custom health check <envoy_api_field_core.HealthCheck.custom_health_check>`.
* health check: health check connections can now be configured to use http/2.
* health check http filter: added
  :ref:`generic header matching <envoy_api_field_config.filter.http.health_check.v2.HealthCheck.headers>`
  to trigger health check response. Deprecated the endpoint option.
* http: filters can now optionally support
  :ref:`virtual host <envoy_api_field_route.VirtualHost.per_filter_config>`,
  :ref:`route <envoy_api_field_route.Route.per_filter_config>`, and
  :ref:`weighted cluster <envoy_api_field_route.WeightedCluster.ClusterWeight.per_filter_config>`
  local configuration.
* http: added the ability to pass DNS type Subject Alternative Names of the client certificate in the
  :ref:`config_http_conn_man_headers_x-forwarded-client-cert` header.
* http: local responses to gRPC requests are now sent as trailers-only gRPC responses instead of plain HTTP responses.
  Notably the HTTP response code is always "200" in this case, and the gRPC error code is carried in "grpc-status"
  header, optionally accompanied with a text message in "grpc-message" header.
* http: added support for :ref:`via header
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.via>`
  append.
* http: added a :ref:`configuration option
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.skip_xff_append>`
  to elide *x-forwarded-for* header modifications.
* http: fixed a bug in inline headers where addCopy and addViaMove didn't add header values when
  encountering inline headers with multiple instances.
* listeners: added :ref:`tcp_fast_open_queue_length <envoy_api_field_Listener.tcp_fast_open_queue_length>` option.
* listeners: added the ability to match :ref:`FilterChain <envoy_api_msg_listener.FilterChain>` using
  :ref:`application_protocols <envoy_api_field_listener.FilterChainMatch.application_protocols>`
  (e.g. ALPN for TLS protocol).
* listeners: `sni_domains` has been deprecated/renamed to :ref:`server_names <envoy_api_field_listener.FilterChainMatch.server_names>`.
* listeners: removed restriction on all filter chains having identical filters.
* load balancer: added :ref:`weighted round robin
  <arch_overview_load_balancing_types_round_robin>` support. The round robin
  scheduler now respects endpoint weights and also has improved fidelity across
  picks.
* load balancer: :ref:`locality weighted load balancing
  <arch_overview_load_balancer_subsets>` is now supported.
* load balancer: ability to configure zone aware load balancer settings :ref:`through the API
  <envoy_api_field_Cluster.CommonLbConfig.zone_aware_lb_config>`.
* load balancer: the :ref:`weighted least request
  <arch_overview_load_balancing_types_least_request>` load balancing algorithm has been improved
  to have better balance when operating in weighted mode.
* logger: added the ability to optionally set the log format via the :option:`--log-format` option.
* logger: all :ref:`logging levels <operations_admin_interface_logging>` can be configured
  at run-time: trace debug info warning error critical.
* rbac http filter: a :ref:`role-based access control http filter <config_http_filters_rbac>` has been added.
* router: the behavior of per-try timeouts have changed in the case where a portion of the response has
  already been proxied downstream when the timeout occurs. Previously, the response would be reset
  leading to either an HTTP/2 reset or an HTTP/1 closed connection and a partial response. Now, the
  timeout will be ignored and the response will continue to proxy up to the global request timeout.
* router: changed the behavior of :ref:`source IP routing <envoy_api_field_route.RouteAction.HashPolicy.ConnectionProperties.source_ip>`
  to ignore the source port.
* router: added an :ref:`prefix_match <envoy_api_field_route.HeaderMatcher.prefix_match>` match type
  to explicitly match based on the prefix of a header value.
* router: added an :ref:`suffix_match <envoy_api_field_route.HeaderMatcher.suffix_match>` match type
  to explicitly match based on the suffix of a header value.
* router: added an :ref:`present_match <envoy_api_field_route.HeaderMatcher.present_match>` match type
  to explicitly match based on a header's presence.
* router: added an :ref:`invert_match <envoy_api_field_route.HeaderMatcher.invert_match>` config option
  which supports inverting all other match types to match based on headers which are not a desired value.
* router: allow :ref:`cookie routing <envoy_api_msg_route.RouteAction.HashPolicy.Cookie>` to
  generate session cookies.
* router: added START_TIME as one of supported variables in :ref:`header
  formatters <config_http_conn_man_headers_custom_request_headers>`.
* router: added a :ref:`max_grpc_timeout <envoy_api_field_route.RouteAction.max_grpc_timeout>`
  config option to specify the maximum allowable value for timeouts decoded from gRPC header field
  `grpc-timeout`.
* router: added a :ref:`configuration option
  <envoy_api_field_config.filter.http.router.v2.Router.suppress_envoy_headers>` to disable *x-envoy-*
  header generation.
* router: added 'unavailable' to the retriable gRPC status codes that can be specified
  through :ref:`x-envoy-retry-grpc-on <config_http_filters_router_x-envoy-retry-grpc-on>`.
* sockets: added :ref:`tap transport socket extension <operations_traffic_tapping>` to support
  recording plain text traffic and PCAP generation.
* sockets: added `IP_FREEBIND` socket option support for :ref:`listeners
  <envoy_api_field_Listener.freebind>` and upstream connections via
  :ref:`cluster manager wide
  <envoy_api_field_config.bootstrap.v2.ClusterManager.upstream_bind_config>` and
  :ref:`cluster specific <envoy_api_field_Cluster.upstream_bind_config>` options.
* sockets: added `IP_TRANSPARENT` socket option support for :ref:`listeners
  <envoy_api_field_Listener.transparent>`.
* sockets: added `SO_KEEPALIVE` socket option for upstream connections
  :ref:`per cluster <envoy_api_field_Cluster.upstream_connection_options>`.
* stats: added support for histograms.
* stats: added :ref:`option to configure the statsd prefix<envoy_api_field_config.metrics.v2.StatsdSink.prefix>`.
* stats: updated stats sink interface to flush through a single call.
* tls: added support for
  :ref:`verify_certificate_spki <envoy_api_field_auth.CertificateValidationContext.verify_certificate_spki>`.
* tls: added support for multiple
  :ref:`verify_certificate_hash <envoy_api_field_auth.CertificateValidationContext.verify_certificate_hash>`
  values.
* tls: added support for using
  :ref:`verify_certificate_spki <envoy_api_field_auth.CertificateValidationContext.verify_certificate_spki>`
  and :ref:`verify_certificate_hash <envoy_api_field_auth.CertificateValidationContext.verify_certificate_hash>`
  without :ref:`trusted_ca <envoy_api_field_auth.CertificateValidationContext.trusted_ca>`.
* tls: added support for allowing expired certificates with
  :ref:`allow_expired_certificate <envoy_api_field_auth.CertificateValidationContext.allow_expired_certificate>`.
* tls: added support for :ref:`renegotiation <envoy_api_field_auth.UpstreamTlsContext.allow_renegotiation>`
  when acting as a client.
* tls: removed support for legacy SHA-2 CBC cipher suites.
* tracing: the sampling decision is now delegated to the tracers, allowing the tracer to decide when and if
  to use it. For example, if the :ref:`x-b3-sampled <config_http_conn_man_headers_x-b3-sampled>` header
  is supplied with the client request, its value will override any sampling decision made by the Envoy proxy.
* websocket: support configuring idle_timeout and max_connect_attempts.
* upstream: added support for host override for a request in :ref:`Original destination host request header <arch_overview_load_balancing_types_original_destination_request_header>`.
* header to metadata: added :ref:`HTTP Header to Metadata filter<config_http_filters_header_to_metadata>`.

1.6.0 (March 20, 2018)
======================

* access log: added DOWNSTREAM_REMOTE_ADDRESS, DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT, and
  DOWNSTREAM_LOCAL_ADDRESS :ref:`access log formatters <config_access_log_format>`.
  DOWNSTREAM_ADDRESS access log formatter has been deprecated.
* access log: added less than or equal (LE) :ref:`comparison filter
  <envoy_api_msg_config.filter.accesslog.v2.ComparisonFilter>`.
* access log: added configuration to :ref:`runtime filter
  <envoy_api_msg_config.filter.accesslog.v2.RuntimeFilter>` to set default sampling rate, divisor,
  and whether to use independent randomness or not.
* admin: added :ref:`/runtime <operations_admin_interface_runtime>` admin endpoint to read the
  current runtime values.
* build: added support for :repo:`building Envoy with exported symbols
  <bazel#enabling-optional-features>`. This change allows scripts loaded with the Lua filter to
  load shared object libraries such as those installed via `LuaRocks <https://luarocks.org/>`_.
* config: added support for sending error details as
  `grpc.rpc.Status <https://github.com/googleapis/googleapis/blob/master/google/rpc/status.proto>`_
  in :ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>`.
* config: added support for :ref:`inline delivery <envoy_api_msg_core.DataSource>` of TLS
  certificates and private keys.
* config: added restrictions for the backing :ref:`config sources <envoy_api_msg_core.ConfigSource>`
  of xDS resources. For filesystem based xDS the file must exist at configuration time. For cluster
  based xDS the backing cluster must be statically defined and be of non-EDS type.
* grpc: the Google gRPC C++ library client is now supported as specified in the :ref:`gRPC services
  overview <arch_overview_grpc_services>` and :ref:`GrpcService <envoy_api_msg_core.GrpcService>`.
* grpc-json: added support for :ref:`inline descriptors
  <envoy_api_field_config.filter.http.transcoder.v2.GrpcJsonTranscoder.proto_descriptor_bin>`.
* health check: added :ref:`gRPC health check <envoy_api_field_core.HealthCheck.grpc_health_check>`
  based on `grpc.health.v1.Health <https://github.com/grpc/grpc/blob/master/src/proto/grpc/health/v1/health.proto>`_
  service.
* health check: added ability to set :ref:`host header value
  <envoy_api_field_core.HealthCheck.HttpHealthCheck.host>` for http health check.
* health check: extended the health check filter to support computation of the health check response
  based on the :ref:`percentage of healthy servers in upstream clusters
  <envoy_api_field_config.filter.http.health_check.v2.HealthCheck.cluster_min_healthy_percentages>`.
* health check: added setting for :ref:`no-traffic
  interval<envoy_api_field_core.HealthCheck.no_traffic_interval>`.
* http: added idle timeout for :ref:`upstream http connections
  <envoy_api_field_core.HttpProtocolOptions.idle_timeout>`.
* http: added support for :ref:`proxying 100-Continue responses
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.proxy_100_continue>`.
* http: added the ability to pass a URL encoded PEM encoded peer certificate in the
  :ref:`config_http_conn_man_headers_x-forwarded-client-cert` header.
* http: added support for trusting additional hops in the
  :ref:`config_http_conn_man_headers_x-forwarded-for` request header.
* http: added support for :ref:`incoming HTTP/1.0
  <envoy_api_field_core.Http1ProtocolOptions.accept_http_10>`.
* hot restart: added SIGTERM propagation to children to :ref:`hot-restarter.py
  <operations_hot_restarter>`, which enables using it as a parent of containers.
* ip tagging: added :ref:`HTTP IP Tagging filter<config_http_filters_ip_tagging>`.
* listeners: added support for :ref:`listening for both IPv4 and IPv6
  <envoy_api_field_core.SocketAddress.ipv4_compat>` when binding to ::.
* listeners: added support for listening on :ref:`UNIX domain sockets
  <envoy_api_field_core.Address.pipe>`.
* listeners: added support for :ref:`abstract unix domain sockets <envoy_api_msg_core.Pipe>` on
  Linux. The abstract namespace can be used by prepending '@' to a socket path.
* load balancer: added cluster configuration for :ref:`healthy panic threshold
  <envoy_api_field_Cluster.CommonLbConfig.healthy_panic_threshold>` percentage.
* load balancer: added :ref:`Maglev <arch_overview_load_balancing_types_maglev>` consistent hash
  load balancer.
* load balancer: added support for
  :ref:`LocalityLbEndpoints<envoy_api_msg_endpoint.LocalityLbEndpoints>` priorities.
* lua: added headers :ref:`replace() <config_http_filters_lua_header_wrapper>` API.
* lua: extended to support :ref:`metadata object <config_http_filters_lua_metadata_wrapper>` API.
* redis: added local `PING` support to the :ref:`Redis filter <arch_overview_redis>`.
* redis: added `GEORADIUS_RO` and `GEORADIUSBYMEMBER_RO` to the :ref:`Redis command splitter
  <arch_overview_redis>` whitelist.
* router: added DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT, DOWNSTREAM_LOCAL_ADDRESS,
  DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT, PROTOCOL, and UPSTREAM_METADATA :ref:`header
  formatters <config_http_conn_man_headers_custom_request_headers>`. The CLIENT_IP header formatter
  has been deprecated.
* router: added gateway-error :ref:`retry-on <config_http_filters_router_x-envoy-retry-on>` policy.
* router: added support for route matching based on :ref:`URL query string parameters
  <envoy_api_msg_route.QueryParameterMatcher>`.
* router: added support for more granular weighted cluster routing by allowing the :ref:`total_weight
  <envoy_api_field_route.WeightedCluster.total_weight>` to be specified in configuration.
* router: added support for :ref:`custom request/response headers
  <config_http_conn_man_headers_custom_request_headers>` with mixed static and dynamic values.
* router: added support for :ref:`direct responses <envoy_api_field_route.Route.direct_response>`.
  I.e., sending a preconfigured HTTP response without proxying anywhere.
* router: added support for :ref:`HTTPS redirects
  <envoy_api_field_route.RedirectAction.https_redirect>` on specific routes.
* router: added support for :ref:`prefix_rewrite
  <envoy_api_field_route.RedirectAction.prefix_rewrite>` for redirects.
* router: added support for :ref:`stripping the query string
  <envoy_api_field_route.RedirectAction.strip_query>` for redirects.
* router: added support for downstream request/upstream response
  :ref:`header manipulation <config_http_conn_man_headers_custom_request_headers>` in :ref:`weighted
  cluster <envoy_api_msg_route.WeightedCluster>`.
* router: added support for :ref:`range based header matching
  <envoy_api_field_route.HeaderMatcher.range_match>` for request routing.
* squash: added support for the :ref:`Squash microservices debugger <config_http_filters_squash>`.
  Allows debugging an incoming request to a microservice in the mesh.
* stats: added metrics service API implementation.
* stats: added native :ref:`DogStatsd <envoy_api_msg_config.metrics.v2.DogStatsdSink>` support.
* stats: added support for :ref:`fixed stats tag values
  <envoy_api_field_config.metrics.v2.TagSpecifier.fixed_value>` which will be added to all metrics.
* tcp proxy: added support for specifying a :ref:`metadata matcher
  <envoy_api_field_config.filter.network.tcp_proxy.v2.TcpProxy.metadata_match>` for upstream
  clusters in the tcp filter.
* tcp proxy: improved TCP proxy to correctly proxy TCP half-close.
* tcp proxy: added :ref:`idle timeout
  <envoy_api_field_config.filter.network.tcp_proxy.v2.TcpProxy.idle_timeout>`.
* tcp proxy: access logs now bring an IP address without a port when using DOWNSTREAM_ADDRESS.
  Use :ref:`DOWNSTREAM_REMOTE_ADDRESS <config_access_log_format>` instead.
* tracing: added support for dynamically loading an :ref:`OpenTracing tracer
  <envoy_api_msg_config.trace.v2.DynamicOtConfig>`.
* tracing: when using the Zipkin tracer, it is now possible for clients to specify the sampling
  decision (using the :ref:`x-b3-sampled <config_http_conn_man_headers_x-b3-sampled>` header) and
  have the decision propagated through to subsequently invoked services.
* tracing: when using the Zipkin tracer, it is no longer necessary to propagate the
  :ref:`x-ot-span-context <config_http_conn_man_headers_x-ot-span-context>` header.
  See more on trace context propagation :ref:`here <arch_overview_tracing>`.
* transport sockets: added transport socket interface to allow custom implementations of transport
  sockets. A transport socket provides read and write logic with buffer encryption and decryption
  (if applicable). The existing TLS implementation has been refactored with the interface.
* upstream: added support for specifying an :ref:`alternate stats name
  <envoy_api_field_Cluster.alt_stat_name>` while emitting stats for clusters.
* Many small bug fixes and performance improvements not listed.

1.5.0 (December 4, 2017)
========================

* access log: added fields for :ref:`UPSTREAM_LOCAL_ADDRESS and DOWNSTREAM_ADDRESS
  <config_access_log_format>`.
* admin: added :ref:`JSON output <operations_admin_interface_stats>` for stats admin endpoint.
* admin: added basic :ref:`Prometheus output <operations_admin_interface_stats>` for stats admin
  endpoint. Histograms are not currently output.
* admin: added ``version_info`` to the :ref:`/clusters admin endpoint<operations_admin_interface_clusters>`.
* config: the :ref:`v2 API <config_overview_v2>` is now considered production ready.
* config: added --v2-config-only CLI flag.
* cors: added :ref:`CORS filter <config_http_filters_cors>`.
* health check: added :ref:`x-envoy-immediate-health-check-fail
  <config_http_filters_router_x-envoy-immediate-health-check-fail>` header support.
* health check: added :ref:`reuse_connection <envoy_api_field_core.HealthCheck.reuse_connection>` option.
* http: added :ref:`per-listener stats <config_http_conn_man_stats_per_listener>`.
* http: end-to-end HTTP flow control is now complete across both connections, streams, and filters.
* load balancer: added :ref:`subset load balancer <arch_overview_load_balancer_subsets>`.
* load balancer: added ring size and hash :ref:`configuration options
  <envoy_api_msg_Cluster.RingHashLbConfig>`. This used to be configurable via runtime. The runtime
  configuration was deleted without deprecation as we are fairly certain no one is using it.
* log: added the ability to optionally log to a file instead of stderr via the
  :option:`--log-path` option.
* listeners: added :ref:`drain_type <envoy_api_field_Listener.drain_type>` option.
* lua: added experimental :ref:`Lua filter <config_http_filters_lua>`.
* mongo filter: added :ref:`fault injection <config_network_filters_mongo_proxy_fault_injection>`.
* mongo filter: added :ref:`"drain close" <arch_overview_draining>` support.
* outlier detection: added :ref:`HTTP gateway failure type <arch_overview_outlier_detection>`.
  See :ref:`deprecated log <deprecated>`
  for outlier detection stats deprecations in this release.
* redis: the :ref:`redis proxy filter <config_network_filters_redis_proxy>` is now considered
  production ready.
* redis: added :ref:`"drain close" <arch_overview_draining>` functionality.
* router: added :ref:`x-envoy-overloaded <config_http_filters_router_x-envoy-overloaded_set>` support.
* router: added :ref:`regex <envoy_api_field_route.RouteMatch.regex>` route matching.
* router: added :ref:`custom request headers <config_http_conn_man_headers_custom_request_headers>`
  for upstream requests.
* router: added :ref:`downstream IP hashing
  <envoy_api_field_route.RouteAction.HashPolicy.connection_properties>` for HTTP ketama routing.
* router: added :ref:`cookie hashing <envoy_api_field_route.RouteAction.HashPolicy.cookie>`.
* router: added :ref:`start_child_span <envoy_api_field_config.filter.http.router.v2.Router.start_child_span>` option
  to create child span for egress calls.
* router: added optional :ref:`upstream logs <envoy_api_field_config.filter.http.router.v2.Router.upstream_log>`.
* router: added complete :ref:`custom append/override/remove support
  <config_http_conn_man_headers_custom_request_headers>` of request/response headers.
* router: added support to :ref:`specify response code during redirect
  <envoy_api_field_route.RedirectAction.response_code>`.
* router: added :ref:`configuration <envoy_api_field_route.RouteAction.cluster_not_found_response_code>`
  to return either a 404 or 503 if the upstream cluster does not exist.
* runtime: added :ref:`comment capability <config_runtime_comments>`.
* server: change default log level (:option:`-l`) to `info`.
* stats: maximum stat/name sizes and maximum number of stats are now variable via the
  `--max-obj-name-len` and `--max-stats` options.
* tcp proxy: added :ref:`access logging <envoy_api_field_config.filter.network.tcp_proxy.v2.TcpProxy.access_log>`.
* tcp proxy: added :ref:`configurable connect retries
  <envoy_api_field_config.filter.network.tcp_proxy.v2.TcpProxy.max_connect_attempts>`.
* tcp proxy: enable use of :ref:`outlier detector <arch_overview_outlier_detection>`.
* tls: added :ref:`SNI support <faq_how_to_setup_sni>`.
* tls: added support for specifying :ref:`TLS session ticket keys
  <envoy_api_field_auth.DownstreamTlsContext.session_ticket_keys>`.
* tls: allow configuration of the :ref:`min
  <envoy_api_field_auth.TlsParameters.tls_minimum_protocol_version>` and :ref:`max
  <envoy_api_field_auth.TlsParameters.tls_maximum_protocol_version>` TLS protocol versions.
* tracing: added :ref:`custom trace span decorators <envoy_api_field_route.Route.decorator>`.
* Many small bug fixes and performance improvements not listed.

1.4.0 (August 24, 2017)
=======================

* macOS is :repo:`now supported </bazel#quick-start-bazel-build-for-developers>`. (A few features
  are missing such as hot restart and original destination routing).
* YAML is now directly supported for config files.
* Added /routes admin endpoint.
* End-to-end flow control is now supported for TCP proxy, HTTP/1, and HTTP/2. HTTP flow control
  that includes filter buffering is incomplete and will be implemented in 1.5.0.
* Log verbosity :repo:`compile time flag </bazel#log-verbosity>` added.
* Hot restart :repo:`compile time flag </bazel#hot-restart>` added.
* Original destination :ref:`cluster <arch_overview_service_discovery_types_original_destination>`
  and :ref:`load balancer <arch_overview_load_balancing_types_original_destination>` added.
* :ref:`WebSocket <arch_overview_websocket>` is now supported.
* Virtual cluster priorities have been hard removed without deprecation as we are reasonably sure
  no one is using this feature.
* Route `validate_clusters` option added.
* :ref:`x-envoy-downstream-service-node <config_http_conn_man_headers_downstream-service-node>`
  header added.
* :ref:`x-forwarded-client-cert <config_http_conn_man_headers_x-forwarded-client-cert>` header
  added.
* Initial HTTP/1 forward proxy support for absolute URLs has been added.
* HTTP/2 codec settings are now configurable.
* gRPC/JSON transcoder :ref:`filter <config_http_filters_grpc_json_transcoder>` added.
* gRPC web :ref:`filter <config_http_filters_grpc_web>` added.
* Configurable timeout for the rate limit service call in the :ref:`network
  <config_network_filters_rate_limit>` and :ref:`HTTP <config_http_filters_rate_limit>` rate limit
  filters.
* :ref:`x-envoy-retry-grpc-on <config_http_filters_router_x-envoy-retry-grpc-on>` header added.
* :ref:`LDS API <arch_overview_dynamic_config_lds>` added.
* TLS :`require_client_certificate` option added.
* :ref:`Configuration check tool <install_tools_config_load_check_tool>` added.
* :ref:`JSON schema check tool <install_tools_schema_validator_check_tool>` added.
* Config validation mode added via the :option:`--mode` option.
* :option:`--local-address-ip-version` option added.
* IPv6 support is now complete.
* UDP `statsd_ip_address` option added.
* Per-cluster DNS resolvers added.
* :ref:`Fault filter <config_http_filters_fault_injection>` enhancements and fixes.
* Several features are :ref:`deprecated as of the 1.4.0 release <deprecated>`. They
  will be removed at the beginning of the 1.5.0 release cycle. We explicitly call out that the
  `HttpFilterConfigFactory` filter API has been deprecated in favor of
  `NamedHttpFilterConfigFactory`.
* Many small bug fixes and performance improvements not listed.

1.3.0 (May 17, 2017)
====================

* As of this release, we now have an official :repo:`breaking change policy
  </CONTRIBUTING.md#breaking-change-policy>`. Note that there are numerous breaking configuration
  changes in this release. They are not listed here. Future releases will adhere to the policy and
  have clear documentation on deprecations and changes.
* Bazel is now the canonical build system (replacing CMake). There have been a huge number of
  changes to the development/build/test flow. See :repo:`/bazel/README.md` and
  :repo:`/ci/README.md` for more information.
* :ref:`Outlier detection <arch_overview_outlier_detection>` has been expanded to include success
  rate variance, and all parameters are now configurable in both runtime and in the JSON
  configuration.
* TCP level listener and cluster connections now have configurable receive buffer
  limits at which point connection level back pressure is applied.
  Full end to end flow control will be available in a future release.
* :ref:`Redis health checking <config_cluster_manager_cluster_hc>` has been added as an active
  health check type. Full Redis support will be documented/supported in 1.4.0.
* :ref:`TCP health checking <config_cluster_manager_cluster_hc_tcp_health_checking>` now supports a
  "connect only" mode that only checks if the remote server can be connected to without
  writing/reading any data.
* `BoringSSL <https://boringssl.googlesource.com/boringssl>`_ is now the only supported TLS provider.
  The default cipher suites and ECDH curves have been updated with more modern defaults for both
  listener and cluster connections.
* The `header value match` rate limit action has been expanded to include an `expect
  match` parameter.
* Route level HTTP rate limit configurations now do not inherit the virtual host level
  configurations by default. Use `include_vh_rate_limits` to inherit the virtual host
  level options if desired.
* HTTP routes can now add request headers on a per route and per virtual host basis via the
  :ref:`request_headers_to_add <config_http_conn_man_headers_custom_request_headers>` option.
* The :ref:`example configurations <install_ref_configs>` have been refreshed to demonstrate the
  latest features.
* `per_try_timeout_ms` can now be configured in
  a route's retry policy in addition to via the :ref:`x-envoy-upstream-rq-per-try-timeout-ms
  <config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms>` HTTP header.
* HTTP virtual host matching now includes support for prefix wildcard domains (e.g., `*.lyft.com`).
* The default for tracing random sampling has been changed to 100% and is still configurable in
  :ref:`runtime <config_http_conn_man_runtime>`.
* HTTP tracing configuration has been extended to allow tags
  to be populated from arbitrary HTTP headers.
* The :ref:`HTTP rate limit filter <config_http_filters_rate_limit>` can now be applied to internal,
  external, or all requests via the `request_type` option.
* :ref:`Listener binding <config_listeners>` now requires specifying an `address` field. This can be
  used to bind a listener to both a specific address as well as a port.
* The :ref:`MongoDB filter <config_network_filters_mongo_proxy>` now emits a stat for queries that
  do not have `$maxTimeMS` set.
* The :ref:`MongoDB filter <config_network_filters_mongo_proxy>` now emits logs that are fully valid
  JSON.
* The CPU profiler output path is now configurable.
* A watchdog system has been added that can kill the server if a deadlock is detected.
* A :ref:`route table checking tool <install_tools_route_table_check_tool>` has been added that can
  be used to test route tables before use.
* We have added an :ref:`example repo <extending>` that shows how to compile/link a custom filter.
* Added additional cluster wide information related to outlier detection to the :ref:`/clusters
  admin endpoint <operations_admin_interface>`.
* Multiple SANs can now be verified via the `verify_subject_alt_name` setting.
  Additionally, URI type SANs can be verified.
* HTTP filters can now be passed opaque configuration specified on a per route basis.
* By default Envoy now has a built in crash handler that will print a back trace. This behavior can
  be disabled if desired via the ``--define=signal_trace=disabled`` Bazel option.
* Zipkin has been added as a supported :ref:`tracing provider <arch_overview_tracing>`.
* Numerous small changes and fixes not listed here.

1.2.0 (March 7, 2017)
=====================

* :ref:`Cluster discovery service (CDS) API <config_cluster_manager_cds>`.
* :ref:`Outlier detection <arch_overview_outlier_detection>` (passive health checking).
* Envoy configuration is now checked against a JSON schema.
* :ref:`Ring hash <arch_overview_load_balancing_types>` consistent load balancer, as well as HTTP
  consistent hash routing based on a policy.
* Vastly :ref:`enhanced global rate limit configuration <arch_overview_rate_limit>` via the HTTP
  rate limiting filter.
* HTTP routing to a cluster retrieved from a header.
* Weighted cluster HTTP routing.
* Auto host rewrite during HTTP routing.
* Regex header matching during HTTP routing.
* HTTP access log runtime filter.
* LightStep tracer :ref:`parent/child span association <arch_overview_tracing>`.
* :ref:`Route discovery service (RDS) API <config_http_conn_man_rds>`.
* HTTP router :ref:`x-envoy-upstream-rq-timeout-alt-response header
  <config_http_filters_router_x-envoy-upstream-rq-timeout-alt-response>` support.
* *use_original_dst* and *bind_to_port* :ref:`listener options <config_listeners>` (useful for
  iptables based transparent proxy support).
* TCP proxy filter :ref:`route table support <config_network_filters_tcp_proxy>`.
* Configurable stats flush interval.
* Various :ref:`third party library upgrades <install_requirements>`, including using BoringSSL as
  the default SSL provider.
* No longer maintain closed HTTP/2 streams for priority calculations. Leads to substantial memory
  savings for large meshes.
* Numerous small changes and fixes not listed here.

1.1.0 (November 30, 2016)
=========================

* Switch from Jannson to RapidJSON for our JSON library (allowing for a configuration schema in
  1.2.0).
* Upgrade :ref:`recommended version <install_requirements>` of various other libraries.
* Configurable DNS refresh rate for DNS service discovery types.
* Upstream circuit breaker configuration can be :ref:`overridden via runtime
  <config_cluster_manager_cluster_runtime>`.
* :ref:`Zone aware routing support <arch_overview_load_balancing_zone_aware_routing>`.
* Generic header matching routing rule.
* HTTP/2 graceful connection draining (double GOAWAY).
* DynamoDB filter :ref:`per shard statistics <config_http_filters_dynamo>` (pre-release AWS
  feature).
* Initial release of the :ref:`fault injection HTTP filter <config_http_filters_fault_injection>`.
* HTTP :ref:`rate limit filter <config_http_filters_rate_limit>` enhancements (note that the
  configuration for HTTP rate limiting is going to be overhauled in 1.2.0).
* Added :ref:`refused-stream retry policy <config_http_filters_router_x-envoy-retry-on>`.
* Multiple :ref:`priority queues <arch_overview_http_routing_priority>` for upstream clusters
  (configurable on a per route basis, with separate connection pools, circuit breakers, etc.).
* Added max connection circuit breaking to the :ref:`TCP proxy filter <arch_overview_tcp_proxy>`.
* Added :ref:`CLI <operations_cli>` options for setting the logging file flush interval as well
  as the drain/shutdown time during hot restart.
* A very large number of performance enhancements for core HTTP/TCP proxy flows as well as a
  few new configuration flags to allow disabling expensive features if they are not needed
  (specifically request ID generation and dynamic response code stats).
* Support Mongo 3.2 in the :ref:`Mongo sniffing filter <config_network_filters_mongo_proxy>`.
* Lots of other small fixes and enhancements not listed.

1.0.0 (September 12, 2016)
==========================

Initial open source release.
