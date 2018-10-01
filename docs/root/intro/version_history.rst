Version history
---------------

1.8.0 (Pending)
===============
* access log: added :ref:`response flag filter <envoy_api_msg_config.filter.accesslog.v2.ResponseFlagFilter>`
  to filter based on the presence of Envoy response flags.
* access log: added RESPONSE_DURATION and RESPONSE_TX_DURATION.
* access log: added REQUESTED_SERVER_NAME for SNI to tcp_proxy and http
* admin: added :http:get:`/hystrix_event_stream` as an endpoint for monitoring envoy's statistics
  through `Hystrix dashboard <https://github.com/Netflix-Skunkworks/hystrix-dashboard/wiki>`_.
* cli: Added support for :ref:`component log level <operations_cli>` command line option for configuring log levels of individual components.
* cluster: added :ref:`option <envoy_api_field_Cluster.CommonLbConfig.update_merge_window>` to merge
  health check/weight/metadata updates within the given duration.
* config: regex validation added to limit to a maximum of 1024 characters.
* config: v1 disabled by default. v1 support remains available until October via flipping --v2-config-only=false.
* config: v1 disabled by default. v1 support remains available until October via setting :option:`--allow-deprecated-v1-api`.
* config: Fixed stat inconsistency between xDS and ADS implementation. :ref:`update_failure <config_cluster_manager_cds>`  
  stat is incremented in case of network failure and :ref:`update_rejected <config_cluster_manager_cds>` stat is incremented 
  in case of schema/validation error.
* config: Added a stat :ref:`connected_state <management_server_stats>` that indicates current connected state of Envoy with 
  management server. 
* ext_authz: added support for configuring additional :ref:`authorization headers <envoy_api_field_config.filter.http.ext_authz.v2alpha.HttpService.authorization_headers_to_add>`
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
* jwt-authn filter: add support for per route JWT requirements.
* listeners: added the ability to match :ref:`FilterChain <envoy_api_msg_listener.FilterChain>` using
  :ref:`destination_port <envoy_api_field_listener.FilterChainMatch.destination_port>` and
  :ref:`prefix_ranges <envoy_api_field_listener.FilterChainMatch.prefix_ranges>`.
* lua: added :ref:`connection() <config_http_filters_lua_connection_wrapper>` wrapper and *ssl()* API.
* lua: added :ref:`requestInfo() <config_http_filters_lua_request_info_wrapper>` wrapper and *protocol()* API.
* lua: added :ref:`requestInfo():dynamicMetadata() <config_http_filters_lua_request_info_dynamic_metadata_wrapper>` API.
* network: introduced :ref:`sni_cluster <config_network_filters_sni_cluster>` network filter that forwards connections to the
  upstream cluster specified by the SNI value presented by the client during a TLS handshake.
* proxy_protocol: added support for HAProxy Proxy Protocol v2 (AF_INET/AF_INET6 only).
* ratelimit: added support for :repo:`api/envoy/service/ratelimit/v2/rls.proto`.
  Lyft's reference implementation of the `ratelimit <https://github.com/lyft/ratelimit>`_ service also supports the data-plane-api proto as of v1.1.0.
  Envoy can use either proto to send client requests to a ratelimit server with the use of the
  :ref:`use_data_plane_proto<envoy_api_field_config.ratelimit.v2.RateLimitServiceConfig.use_data_plane_proto>`
  boolean flag in the ratelimit configuration.
  Support for the legacy proto :repo:`source/common/ratelimit/ratelimit.proto` is deprecated and will be removed at the start of the 1.9.0 release cycle.
* ratelimit: added :ref:`failure_mode_deny <envoy_api_msg_config.filter.http.rate_limit.v2.RateLimit>` option to control traffic flow in 
  case of rate limit service error.
* rbac config: added a :ref:`principal_name <envoy_api_field_config.rbac.v2alpha.Principal.Authenticated.principal_name>` field and
  removed the old `name` field to give more flexibility for matching certificate identity.
* rbac network filter: a :ref:`role-based access control network filter <config_network_filters_rbac>` has been added.
* rest-api: added ability to set the :ref:`request timeout <envoy_api_field_core.ApiConfigSource.request_timeout>` for REST API requests.
* route checker: Added v2 config support and removed support for v1 configs.
* router: added ability to set request/response headers at the :ref:`envoy_api_msg_route.Route` level.
* tcp_proxy: added support for :ref:`weighted clusters <envoy_api_field_config.filter.network.tcp_proxy.v2.TcpProxy.weighted_clusters>`.
* thrift_proxy: introduced thrift routing, moved configuration to correct location
* thrift_proxy: introduced thrift configurable decoder filters
* tls: implemented :ref:`Secret Discovery Service <config_secret_discovery_service>`.
* tracing: added support for configuration of :ref:`tracing sampling
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.tracing>`.
* upstream: added configuration option to the subset load balancer to take locality weights into account when
  selecting a host from a subset.
* upstream: require opt-in to use the :ref:`x-envoy-orignal-dst-host <config_http_conn_man_headers_x-envoy-original-dst-host>` header
  for overriding destination address when using the :ref:`Original Destination <arch_overview_load_balancing_types_original_destination>`
  load balancing policy.

1.7.0
===============
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
* sockets: added :ref:`capture transport socket extension <operations_traffic_capture>` to support
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
* websocket: support configuring
  :ref:`idle_timeout and max_connect_attempts <envoy_api_field_route.RouteAction.websocket_config>`.
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
* config: added :option:`--v2-config-only` CLI flag.
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
  See `DEPRECATED.md <https://github.com/envoyproxy/envoy/blob/master/DEPRECATED.md#version-150>`_
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
  :option:`--max-obj-name-len` and :option:`--max-stats` options.
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
* YAML is now directly supported for :ref:`config files <config_overview_v1>`.
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
* Route :ref:`validate_clusters <config_http_conn_man_route_table_validate_clusters>` option added.
* :ref:`x-envoy-downstream-service-node <config_http_conn_man_headers_downstream-service-node>`
  header added.
* :ref:`x-forwarded-client-cert <config_http_conn_man_headers_x-forwarded-client-cert>` header
  added.
* Initial HTTP/1 forward proxy support for :ref:`absolute URLs
  <config_http_conn_man_http1_settings>` has been added.
* HTTP/2 codec settings are now :ref:`configurable <config_http_conn_man_http2_settings>`.
* gRPC/JSON transcoder :ref:`filter <config_http_filters_grpc_json_transcoder>` added.
* gRPC web :ref:`filter <config_http_filters_grpc_web>` added.
* Configurable timeout for the rate limit service call in the :ref:`network
  <config_network_filters_rate_limit>` and :ref:`HTTP <config_http_filters_rate_limit>` rate limit
  filters.
* :ref:`x-envoy-retry-grpc-on <config_http_filters_router_x-envoy-retry-grpc-on>` header added.
* :ref:`LDS API <arch_overview_dynamic_config_lds>` added.
* TLS :ref:`require_client_certificate <config_listener_ssl_context_require_client_certificate>`
  option added.
* :ref:`Configuration check tool <install_tools_config_load_check_tool>` added.
* :ref:`JSON schema check tool <install_tools_schema_validator_check_tool>` added.
* Config validation mode added via the :option:`--mode` option.
* :option:`--local-address-ip-version` option added.
* IPv6 support is now complete.
* UDP :ref:`statsd_ip_address <config_overview_statsd_udp_ip_address>` option added.
* Per-cluster :ref:`DNS resolvers <config_cluster_manager_cluster_dns_resolvers>` added.
* :ref:`Fault filter <config_http_filters_fault_injection>` enhancements and fixes.
* Several features are :repo:`deprecated as of the 1.4.0 release </DEPRECATED.md#version-140>`. They
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
* TCP level :ref:`listener <config_listeners_per_connection_buffer_limit_bytes>` and
  :ref:`cluster <config_cluster_manager_cluster_per_connection_buffer_limit_bytes>` connections now
  have configurable receive buffer limits at which point connection level back pressure is applied.
  Full end to end flow control will be available in a future release.
* :ref:`Redis health checking <config_cluster_manager_cluster_hc>` has been added as an active
  health check type. Full Redis support will be documented/supported in 1.4.0.
* :ref:`TCP health checking <config_cluster_manager_cluster_hc_tcp_health_checking>` now supports a
  "connect only" mode that only checks if the remote server can be connected to without
  writing/reading any data.
* `BoringSSL <https://boringssl.googlesource.com/boringssl>`_ is now the only supported TLS provider.
  The default cipher suites and ECDH curves have been updated with more modern defaults for both
  :ref:`listener <config_listener_ssl_context>` and
  :ref:`cluster <config_cluster_manager_cluster_ssl>` connections.
* The `header value match` :ref:`rate limit action
  <config_http_conn_man_route_table_rate_limit_actions>` has been expanded to include an *expect
  match* parameter.
* Route level HTTP rate limit configurations now do not inherit the virtual host level
  configurations by default. The :ref:`include_vh_rate_limits
  <config_http_conn_man_route_table_route_include_vh>` to inherit the virtual host level options if
  desired.
* HTTP routes can now add request headers on a per route and per virtual host basis via the
  :ref:`request_headers_to_add <config_http_conn_man_headers_custom_request_headers>` option.
* The :ref:`example configurations <install_ref_configs>` have been refreshed to demonstrate the
  latest features.
* :ref:`per_try_timeout_ms <config_http_conn_man_route_table_route_retry>` can now be configured in
  a route's retry policy in addition to via the :ref:`x-envoy-upstream-rq-per-try-timeout-ms
  <config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms>` HTTP header.
* :ref:`HTTP virtual host matching <config_http_conn_man_route_table_vhost>` now includes support
  for prefix wildcard domains (e.g., `*.lyft.com`).
* The default for tracing random sampling has been changed to 100% and is still configurable in
  :ref:`runtime <config_http_conn_man_runtime>`.
* :ref:`HTTP tracing configuration <config_http_conn_man_tracing>` has been extended to allow tags
  to be populated from arbitrary HTTP headers.
* The :ref:`HTTP rate limit filter <config_http_filters_rate_limit>` can now be applied to internal,
  external, or all requests via the `request_type` option.
* :ref:`Listener binding <config_listeners>` now requires specifying an `address` field. This can be
  used to bind a listener to both a specific address as well as a port.
* The :ref:`MongoDB filter <config_network_filters_mongo_proxy>` now emits a stat for queries that
  do not have `$maxTimeMS` set.
* The :ref:`MongoDB filter <config_network_filters_mongo_proxy>` now emits logs that are fully valid
  JSON.
* The CPU profiler output path is now :ref:`configurable <config_admin_v1>`.
* A :ref:`watchdog system <config_overview_v1>` has been added that can kill the server if a deadlock
  is detected.
* A :ref:`route table checking tool <install_tools_route_table_check_tool>` has been added that can
  be used to test route tables before use.
* We have added an :ref:`example repo <extending>` that shows how to compile/link a custom filter.
* Added additional cluster wide information related to outlier detection to the :ref:`/clusters
  admin endpoint <operations_admin_interface>`.
* Multiple SANs can now be verified via the :ref:`verify_subject_alt_name
  <config_listener_ssl_context>` setting. Additionally, URI type SANs can be verified.
* HTTP filters can now be passed :ref:`opaque configuration
  <config_http_conn_man_route_table_opaque_config>` specified on a per route basis.
* By default Envoy now has a built in crash handler that will print a back trace. This behavior can
  be disabled if desired via the ``--define=signal_trace=disabled`` Bazel option.
* Zipkin has been added as a supported :ref:`tracing provider <arch_overview_tracing>`.
* Numerous small changes and fixes not listed here.

1.2.0 (March 7, 2017)
=====================

* :ref:`Cluster discovery service (CDS) API <config_cluster_manager_cds>`.
* :ref:`Outlier detection <arch_overview_outlier_detection>` (passive health checking).
* Envoy configuration is now checked against a :ref:`JSON schema <config_overview_v1>`.
* :ref:`Ring hash <arch_overview_load_balancing_types>` consistent load balancer, as well as HTTP
  consistent hash routing :ref:`based on a policy <config_http_conn_man_route_table_hash_policy>`.
* Vastly :ref:`enhanced global rate limit configuration <arch_overview_rate_limit>` via the HTTP
  rate limiting filter.
* HTTP routing to a cluster :ref:`retrieved from a header
  <config_http_conn_man_route_table_route_cluster_header>`.
* :ref:`Weighted cluster <config_http_conn_man_route_table_route_config_weighted_clusters>` HTTP
  routing.
* :ref:`Auto host rewrite <config_http_conn_man_route_table_route_auto_host_rewrite>` during HTTP
  routing.
* :ref:`Regex header matching <config_http_conn_man_route_table_route_headers>` during HTTP routing.
* HTTP access log :ref:`runtime filter <config_http_con_manager_access_log_filters_runtime_v1>`.
* LightStep tracer :ref:`parent/child span association <arch_overview_tracing>`.
* :ref:`Route discovery service (RDS) API <config_http_conn_man_rds>`.
* HTTP router :ref:`x-envoy-upstream-rq-timeout-alt-response header
  <config_http_filters_router_x-envoy-upstream-rq-timeout-alt-response>` support.
* *use_original_dst* and *bind_to_port* :ref:`listener options <config_listeners>` (useful for
  iptables based transparent proxy support).
* TCP proxy filter :ref:`route table support <config_network_filters_tcp_proxy>`.
* Configurable :ref:`stats flush interval <config_overview_stats_flush_interval_ms>`.
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
* :ref:`Configurable DNS refresh rate <config_cluster_manager_cluster_dns_refresh_rate_ms>` for
  DNS service discovery types.
* Upstream circuit breaker configuration can be :ref:`overridden via runtime
  <config_cluster_manager_cluster_runtime>`.
* :ref:`Zone aware routing support <arch_overview_load_balancing_zone_aware_routing>`.
* Generic :ref:`header matching routing rule <config_http_conn_man_route_table_route_headers>`.
* HTTP/2 :ref:`graceful connection draining <config_http_conn_man_drain_timeout_ms>` (double
  GOAWAY).
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
