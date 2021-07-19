1.20.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* grpc: gRPC async client can be cached and shared accross filter instances in the same thread, this feature is turned off by default, can be turned on by setting runtime guard ``envoy.reloadable_features.enable_grpc_async_client_cache`` to true.
* http: set the default :ref:`lazy headermap threshold <arch_overview_http_header_map_settings>` to 3,
  which defines the minimal number of headers in a request/response/trailers required for using a
  dictionary in addition to the list. Setting the `envoy.http.headermap.lazy_map_min_size` runtime
  feature to a non-negative number will override the default value.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed ``envoy.reloadable_features.http_upstream_wait_connect_response`` runtime guard and legacy code paths.
* http: removed ``envoy.reloadable_features.allow_preconnect`` runtime guard and legacy code paths.
* listener: removed ``envoy.reloadable_features.disable_tls_inspector_injection`` runtime guard and legacy code paths.

New Features
------------

* access_log: added the new response flag for :ref:`overload manager termination <envoy_v3_api_field_data.accesslog.v3.ResponseFlags.overload_manager>`. The response flag will be set when the http stream is terminated by overload manager.
* admission control: added :ref:`rps_threshold <envoy_v3_api_field_extensions.filters.http.admission_control.v3alpha.AdmissionControl.rps_threshold>` option that when average RPS of the sampling window is below this threshold, the filter will not throttle requests. Added :ref:`max_rejection_probability <envoy_v3_api_field_extensions.filters.http.admission_control.v3alpha.AdmissionControl.max_rejection_probability>` option to set an upper limit on the probability of rejection.
* bandwidth_limit: added new :ref:`HTTP bandwidth limit filter <config_http_filters_bandwidth_limit>`.
* bootstrap: added :ref:`dns_resolution_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dns_resolution_config>` to aggregate all of the DNS resolver configuration in a single message. By setting ``no_default_search_domain`` to true the DNS resolver will not use the default search domains. By setting the ``resolvers`` the external DNS servers to be used for external DNS queries can be specified.
* cluster: added :ref:`dns_resolution_config <envoy_v3_api_field_config.cluster.v3.Cluster.dns_resolution_config>` to aggregate all of the DNS resolver configuration in a single message. By setting ``no_default_search_domain`` to true the DNS resolver will not use the default search domains.
* cluster: added :ref:`host_rewrite_literal <envoy_v3_api_field_config.route.v3.WeightedCluster.ClusterWeight.host_rewrite_literal>` to WeightedCluster.
* cluster: added :ref:`wait_for_warm_on_init <envoy_v3_api_field_config.cluster.v3.Cluster.wait_for_warm_on_init>`, which allows cluster readiness to not block on cluster warm-up. It is true by default, which preserves existing behavior. Currently, only applicable for DNS-based clusters.
* composite filter: can now be used with filters that also add an access logger, such as the WASM filter.
* config: added stat :ref:`config_reload_time_ms <subscription_statistics>`.
* connection_limit: added new :ref:`Network connection limit filter <config_network_filters_connection_limit>`.
* crash support: restore crash context when continuing to processing requests or responses as a result of an asynchronous callback that invokes a filter directly. This is unlike the call stacks that go through the various network layers, to eventually reach the filter. For a concrete example see: ``Envoy::Extensions::HttpFilters::Cache::CacheFilter::getHeaders`` which posts a callback on the dispatcher that will invoke the filter directly.
* dns cache: added :ref:`preresolve_hostnames <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.preresolve_hostnames>` option to the DNS cache config. This option allows hostnames to be preresolved into the cache upon cache creation. This might provide performance improvement, in the form of cache hits, for hostnames that are going to be resolved during steady state and are known at config load time.
* dns cache: added :ref:`dns_query_timeout <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_query_timeout>` option to the DNS cache config. This option allows explicitly controlling the timeout of underlying queries independently of the underlying DNS platform implementation. Coupled with success and failure retry policies the use of this timeout will lead to more deterministic DNS resolution times.
* dns resolver: added ``DnsResolverOptions`` protobuf message to reconcile all of the DNS lookup option flags. By setting the configuration option :ref:`use_tcp_for_dns_lookups <envoy_v3_api_field_config.core.v3.DnsResolverOptions.use_tcp_for_dns_lookups>` as true we can make the underlying dns resolver library to make only TCP queries to the DNS servers and by setting the configuration option :ref:`no_default_search_domain <envoy_v3_api_field_config.core.v3.DnsResolverOptions.no_default_search_domain>` as true the DNS resolver library will not use the default search domains.
* dns resolver: added ``DnsResolutionConfig`` to combine :ref:`dns_resolver_options <envoy_v3_api_field_config.core.v3.DnsResolutionConfig.dns_resolver_options>` and :ref:`resolvers <envoy_v3_api_field_config.core.v3.DnsResolutionConfig.resolvers>` in a single protobuf message. The field ``resolvers`` can be specified with a list of DNS resolver addresses. If specified, DNS client library will perform resolution via the underlying DNS resolvers. Otherwise, the default system resolvers (e.g., /etc/resolv.conf) will be used.
* dns_filter: added :ref:`dns_resolution_config <envoy_v3_api_field_extensions.filters.udp.dns_filter.v3alpha.DnsFilterConfig.ClientContextConfig.dns_resolution_config>` to aggregate all of the DNS resolver configuration in a single message. By setting the configuration option ``use_tcp_for_dns_lookups`` to true we can make dns filter's external resolvers to answer queries using TCP only, by setting the configuration option ``no_default_search_domain`` as true the DNS resolver will not use the default search domains. And by setting the configuration ``resolvers`` we can specify the external DNS servers to be used for external DNS query which replaces the pre-existing alpha api field ``upstream_resolvers``.
* dynamic_forward_proxy: added :ref:`dns_resolution_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_resolution_config>` option to the DNS cache config in order to aggregate all of the DNS resolver configuration in a single message. By setting one such configuration option ``no_default_search_domain`` as true the DNS resolver will not use the default search domains. And by setting the configuration ``resolvers`` we can specify the external DNS servers to be used for external DNS query instead of the system default resolvers.
* ext_authz_filter: added :ref:`bootstrap_metadata_labels_key <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.bootstrap_metadata_labels_key>` option to configure labels of destination service.
* http: added new field ``is_optional`` to ``extensions.filters.network.http_connection_manager.v3.HttpFilter``. When
  set to ``true``, unsupported http filters will be ignored by envoy. This is also same with unsupported http filter
  in the typed per filter config. For more information, please reference
  :ref:`HttpFilter <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.is_optional>`.
* http: added :ref:`scheme options <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.scheme_header_transformation>` for adding or overwriting scheme.
* http: added :ref:`stripping trailing host dot from host header <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.strip_trailing_host_dot>` support.
* http: added support for :ref:`original IP detection extensions <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.original_ip_detection_extensions>`.
  Two initial extensions were added, the :ref:`custom header <envoy_v3_api_msg_extensions.http.original_ip_detection.custom_header.v3.CustomHeaderConfig>` extension and the
  :ref:`xff <envoy_v3_api_msg_extensions.http.original_ip_detection.xff.v3.XffConfig>` extension.
* http: added a new option to upstream HTTP/2 :ref:`keepalive <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.connection_keepalive>` to send a PING ahead of a new stream if the connection has been idle for a sufficient duration.
* http: added the ability to :ref:`unescape slash sequences <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.path_with_escaped_slashes_action>` in the path. Requests with unescaped slashes can be proxied, rejected or redirected to the new unescaped path. By default this feature is disabled. The default behavior can be overridden through :ref:`http_connection_manager.path_with_escaped_slashes_action<config_http_conn_man_runtime_path_with_escaped_slashes_action>` runtime variable. This action can be selectively enabled for a portion of requests by setting the :ref:`http_connection_manager.path_with_escaped_slashes_action_sampling<config_http_conn_man_runtime_path_with_escaped_slashes_action_enabled>` runtime variable.
* http: added upstream and downstream alpha HTTP/3 support! See :ref:`quic_options <envoy_v3_api_field_config.listener.v3.UdpListenerConfig.quic_options>` for downstream and the new http3_protocol_options in :ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>` for upstream HTTP/3.
* http: raise max configurable max_request_headers_kb limit to 8192 KiB (8MiB) from 96 KiB in http connection manager.
* input matcher: added a new input matcher that :ref:`matches an IP address against a list of CIDR ranges <envoy_v3_api_file_envoy/extensions/matching/input_matchers/ip/v3/ip.proto>`.
* jwt_authn: added support to fetch remote jwks asynchronously specified by :ref:`async_fetch <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.RemoteJwks.async_fetch>`.
* jwt_authn: added support to add padding in the forwarded JWT payload specified by :ref:`pad_forward_payload_header <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.pad_forward_payload_header>`.
* listener: added ability to change an existing listener's address.
* listener: added filter chain match support for :ref:`direct source address <envoy_v3_api_field_config.listener.v3.FilterChainMatch.direct_source_prefix_ranges>`.
* local_rate_limit_filter: added suppoort for locally rate limiting http requests on a per connection basis. This can be enabled by setting the :ref:`local_rate_limit_per_downstream_connection <envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.local_rate_limit_per_downstream_connection>` field to true.
* metric service: added support for sending metric tags as labels. This can be enabled by setting the :ref:`emit_tags_as_labels <envoy_v3_api_field_config.metrics.v3.MetricsServiceConfig.emit_tags_as_labels>` field to true.
* proxy protocol: added support for generating the header while using the :ref:`HTTP connection manager <config_http_conn_man>`. This is done using the :ref:`Proxy Protocol Transport Socket <extension_envoy.transport_sockets.upstream_proxy_protocol>` on upstream clusters.
  This feature is currently affected by a memory leak `issue <https://github.com/envoyproxy/envoy/issues/16682>`_.
* redis_proxy: added :ref:`hotkey <envoy_v3_api_msg_extensions.filters.network.redis_proxy.v3.RedisProxy.HotKey>` to find hotkeys by heat.
* req_without_query: added access log formatter extension implementing command operator :ref:`REQ_WITHOUT_QUERY <envoy_v3_api_msg_extensions.formatter.req_without_query.v3.ReqWithoutQuery>` to log the request path, while excluding the query string.
* router: added option ``suppress_grpc_request_failure_code_stats`` to :ref:`the router <envoy_v3_api_msg_extensions.filters.http.router.v3.Router>` to allow users to exclude incrementing HTTP status code stats on gRPC requests.
* stats: added native :ref:`Graphite-formatted tag <envoy_v3_api_msg_extensions.stat_sinks.graphite_statsd.v3.GraphiteStatsdSink>` support.
* tcp: added support for :ref:`preconnecting <v1.18.0:envoy_v3_api_msg_config.cluster.v3.Cluster.PreconnectPolicy>`. Preconnecting is off by default, but recommended for clusters serving latency-sensitive traffic.
* thrift_proxy: added per upstream metrics within the :ref:`thrift router <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.router.v3.Router>` for request and response size histograms.
* thrift_proxy: added support for :ref:`outlier detection <arch_overview_outlier_detection>`.
* tls: allow dual ECDSA/RSA certs via SDS. Previously, SDS only supported a single certificate per context, and dual cert was only supported via non-SDS.
* tracing: add option :ref:`use_request_id_for_trace_sampling <envoy_v3_api_field_extensions.request_id.uuid.v3.UuidRequestIdConfig.use_request_id_for_trace_sampling>` which allows configuring whether to perform sampling based on :ref:`x-request-id<config_http_conn_man_headers_x-request-id>` or not.
* udp_proxy: added :ref:`key <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.HashPolicy>` as another hash policy to support hash based routing on any given key.
* windows container image: added user, EnvoyUser which is part of the Network Configuration Operators group to the container image.
* http: added :ref:`string_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.string_match>` in the header matcher.

Deprecated
----------
* http: the HeaderMatcher fields :ref:`exact_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.exact_match>`, :ref:`safe_regex_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.safe_regex_match>`,
  :ref:`prefix_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.prefix_match>`, :ref:`suffix_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.suffix_match>` and
  :ref:`contains_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.contains_match>` are deprecated by :ref:`string_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.string_match>`.
