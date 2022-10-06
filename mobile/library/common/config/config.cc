// NOLINT(namespace-envoy)
#include "library/common/config/internal.h"
#include "library/common/config/templates.h"

const int custom_cluster_indent = 2;
const int custom_listener_indent = 2;
const int custom_filter_indent = 2;
const int custom_route_indent = 16;

const int fake_remote_response_indent = 14;
const char* fake_remote_cluster_insert = "  - *fake_remote_cluster\n";
const char* fake_remote_listener_insert = "  - *fake_remote_listener\n";
const char* fake_remote_route_insert = "              - *fake_remote_route\n";

const char* platform_filter_template = R"(
  - name: envoy.filters.http.platform_bridge
    typed_config:
      "@type": type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge
      platform_filter_name: {{ platform_filter_name }}
)";

const char* native_filter_template = R"(
  - name: {{ native_filter_name }}
    typed_config: {{ native_filter_typed_config }}
)";

const char* route_cache_reset_filter_insert = R"(
  - name: envoy.filters.http.route_cache_reset
    typed_config:
      "@type": type.googleapis.com/envoymobile.extensions.filters.http.route_cache_reset.RouteCacheReset
)";

const char* alternate_protocols_cache_filter_insert = R"(
  - name: alternate_protocols_cache
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.alternate_protocols_cache.v3.FilterConfig
      alternate_protocols_cache_options:
        name: default_alternate_protocols_cache
)";

const char* gzip_config_insert = R"(
  - name: envoy.filters.http.decompressor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.decompressor.v3.Decompressor
      decompressor_library:
        name: gzip
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip
          window_bits: 15
      request_direction_config:
        common_config:
          enabled:
            default_value: false
            runtime_key: request_decompressor_enabled
      response_direction_config:
        common_config:
          ignore_no_transform_header: true
)";

const char* brotli_config_insert = R"(
  - name: envoy.filters.http.decompressor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.decompressor.v3.Decompressor
      decompressor_library:
        name: text_optimized
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.compression.brotli.decompressor.v3.Brotli
      request_direction_config:
        common_config:
          enabled:
            default_value: false
            runtime_key: request_decompressor_enabled
      response_direction_config:
        common_config:
          ignore_no_transform_header: true
)";

const char* socket_tag_config_insert = R"(
  - name: envoy.filters.http.socket_tag
    typed_config:
      "@type": type.googleapis.com/envoymobile.extensions.filters.http.socket_tag.SocketTag
)";

// clang-format off
const std::string config_header = R"(
!ignore default_defs:
- &connect_timeout 30s
- &dns_fail_base_interval 2s
- &dns_fail_max_interval 10s
- &dns_lookup_family ALL
- &dns_min_refresh_rate 60s
- &dns_multiple_addresses true
- &dns_preresolve_hostnames []
- &dns_query_timeout 25s
- &dns_refresh_rate 60s
- &force_ipv6 false
)"
#if defined(__APPLE__)
R"(- &dns_resolver_name envoy.network.dns_resolver.apple
- &dns_resolver_config {"@type":"type.googleapis.com/envoy.extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig"}
)"
#else
R"(- &dns_resolver_name envoy.network.dns_resolver.cares
- &dns_resolver_config {"@type":"type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig"}
)"
#endif
R"(- &enable_drain_post_dns_refresh false
- &enable_interface_binding false
- &h2_connection_keepalive_idle_interval 100000s
- &h2_connection_keepalive_timeout 10s
- &h2_delay_keepalive_timeout false
- &max_connections_per_host 7
- &metadata {}
- &stats_domain 127.0.0.1
- &stats_flush_interval 60s
- &stats_sinks []
- &stream_idle_timeout 15s
- &per_try_idle_timeout 15s
- &trust_chain_verification VERIFY_TRUST_CHAIN
- &virtual_clusters []

!ignore stats_defs:
  base_metrics_service: &base_metrics_service
    name: envoy.metrics_service
    typed_config:
      "@type": type.googleapis.com/envoy.config.metrics.v3.MetricsServiceConfig
      transport_api_version: V3
      report_counters_as_deltas: true
      emit_tags_as_labels: true
      grpc_service:
        envoy_grpc:
          cluster_name: stats

!ignore admin_interface_defs: &admin_interface
    address:
      socket_address:
        address: ::1
        port_value: 9901

!ignore tls_root_ca_defs: &tls_root_certs |
)"
#include "certificates.inc"
R"(

!ignore tls_socket_defs:
- &base_tls_socket
  name: envoy.transport_sockets.http_11_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.http_11_proxy.v3.Http11ProxyUpstreamTransport
    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
        common_tls_context:
          tls_params:
            tls_maximum_protocol_version: TLSv1_3
          validation_context:
            trusted_ca:
              inline_string: *tls_root_certs
)";

const char* config_template = R"(
!ignore local_error_defs: &local_error_config
  "@type": type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError

!ignore network_defs: &network_configuration_config
  "@type": type.googleapis.com/envoymobile.extensions.filters.http.network_configuration.NetworkConfiguration
  enable_drain_post_dns_refresh: *enable_drain_post_dns_refresh
  enable_interface_binding: *enable_interface_binding

!ignore dfp_defs: &dfp_config
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
  dns_cache_config: &dns_cache_config
    name: base_dns_cache
    preresolve_hostnames: *dns_preresolve_hostnames
    dns_lookup_family: *dns_lookup_family
    host_ttl: 86400s
    dns_min_refresh_rate: *dns_min_refresh_rate
    dns_refresh_rate: *dns_refresh_rate
    dns_failure_refresh_rate:
      base_interval: *dns_fail_base_interval
      max_interval: *dns_fail_max_interval
    dns_query_timeout: *dns_query_timeout
    typed_dns_resolver_config:
      name: *dns_resolver_name
      typed_config: *dns_resolver_config

!ignore router_defs: &router_config
  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router

!ignore http_filter_defs: &http_filters
#{custom_filters}
  - name: envoy.filters.http.network_configuration
    typed_config: *network_configuration_config
  - name: envoy.filters.http.local_error
    typed_config: *local_error_config
  - name: envoy.filters.http.dynamic_forward_proxy
    typed_config: *dfp_config
  - name: envoy.router
    typed_config: *router_config

!ignore protocol_options_defs:
- &h1_protocol_options
  envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
    "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
    explicit_http_config:
      http_protocol_options: &h1_config
        header_key_format:
          stateful_formatter:
            name: preserve_case
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.http.header_formatters.preserve_case.v3.PreserveCaseFormatterConfig
              forward_reason_phrase: false
              formatter_type_on_envoy_headers: DEFAULT
    upstream_http_protocol_options: &upstream_http_protocol_options
      auto_sni: true
      auto_san_validation: true
- &h2_protocol_options
  envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
    "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
    explicit_http_config:
      http2_protocol_options: &h2_config
        connection_keepalive:
          connection_idle_interval: *h2_connection_keepalive_idle_interval
          timeout: *h2_connection_keepalive_timeout
        max_concurrent_streams: 100
    upstream_http_protocol_options: *upstream_http_protocol_options
- &alpn_protocol_options
  envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
    "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
    auto_config:
      http2_protocol_options: *h2_config
      http_protocol_options: *h1_config
    upstream_http_protocol_options: *upstream_http_protocol_options
- &h3_protocol_options
  envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
    "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
    auto_config:
      alternate_protocols_cache_options:
        name: default_alternate_protocols_cache
      http3_protocol_options: {}
      http2_protocol_options: *h2_config
      http_protocol_options: *h1_config
    upstream_http_protocol_options: *upstream_http_protocol_options

!ignore custom_listener_defs:
  fake_remote_listener: &fake_remote_listener
    name: fake_remote_listener
    address:
      socket_address: { protocol: TCP, address: 127.0.0.1, port_value: 10101 }
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: remote_hcm
          route_config:
            name: remote_route
            virtual_hosts:
            - name: remote_service
              domains: ["*"]
              routes:
#{fake_remote_responses}
              - match: { prefix: "/" }
                direct_response: { status: 404, body: { inline_string: "not found" } }
                request_headers_to_remove:
                - x-forwarded-proto
                - x-envoy-mobile-cluster
          http_filters:
          - name: envoy.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router

!ignore custom_cluster_defs:
  stats_cluster: &stats_cluster
    name: stats
    type: LOGICAL_DNS
    wait_for_warm_on_init: false
    connect_timeout: *connect_timeout
    dns_refresh_rate: *dns_refresh_rate
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http2_protocol_options: {}
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: stats
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address: { address: *stats_domain, port_value: 443 }
    transport_socket: *base_tls_socket
  fake_remote_cluster: &fake_remote_cluster
    name: fake_remote
    type: LOGICAL_DNS
    connect_timeout: *connect_timeout
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: fake_remote
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address: { address: 127.0.0.1, port_value: 10101 }

typed_dns_resolver_config:
  name: *dns_resolver_name
  typed_config: *dns_resolver_config

static_resources:
  listeners:
#{custom_listeners}
  - name: base_api_listener
    address:
      socket_address:
        protocol: TCP
        address: 0.0.0.0
        port_value: 10000
    per_connection_buffer_limit_bytes: 10485760 # 10MB
    api_listener:
      api_listener:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager
        config:
          stat_prefix: hcm
          server_header_transformation: PASS_THROUGH
          stream_idle_timeout: *stream_idle_timeout
          route_config:
            name: api_router
            virtual_hosts:
)"
// The list of virtual hosts impacts directly the number of virtual cluster stats.
// That's because we create a separate set of virtual clusters stats for every
// "virtual cluster" <> "virtual host" pair. Increasing the number of virtual hosts
// from 1 to 2 doubles the number of virtual cluster stats.
R"(
            - name: api
              include_attempt_count_in_response: true
              virtual_clusters: *virtual_clusters
              domains: ["*"]
              routes:
#{custom_routes}
              - match: { prefix: "/" }
                request_headers_to_remove:
                - x-forwarded-proto
                - x-envoy-mobile-cluster
                route:
                  cluster_header: x-envoy-mobile-cluster
                  timeout: 0s
                  retry_policy:
                    per_try_idle_timeout: *per_try_idle_timeout
                    retry_back_off:
                      base_interval: 0.25s
                      max_interval: 60s
          http_filters: *http_filters
  clusters:
#{custom_clusters}
  - *stats_cluster
  - name: base
    connect_timeout: *connect_timeout
    lb_policy: CLUSTER_PROVIDED
    cluster_type: &base_cluster_type
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config: *dns_cache_config
    transport_socket: *base_tls_socket
    upstream_connection_options: &upstream_opts
      set_local_interface_name_on_upstream_connections: true
      tcp_keepalive:
        keepalive_interval: 5
        keepalive_probes: 1
        keepalive_time: 10
    circuit_breakers: &circuit_breakers_settings
      thresholds:
      - priority: DEFAULT
        # Don't impose limits on concurrent retries.
        retry_budget:
          budget_percent:
            value: 100
          min_retry_concurrency: 0xffffffff # uint32 max
      per_host_thresholds:
      - priority: DEFAULT
        max_connections: *max_connections_per_host
    typed_extension_protocol_options: *alpn_protocol_options
  - name: base_clear
    connect_timeout: *connect_timeout
    lb_policy: CLUSTER_PROVIDED
    cluster_type: *base_cluster_type
    transport_socket:
      name: envoy.transport_sockets.http_11_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.http_11_proxy.v3.Http11ProxyUpstreamTransport
        transport_socket:
          name: envoy.transport_sockets.raw_buffer
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
    upstream_connection_options: *upstream_opts
    circuit_breakers: *circuit_breakers_settings
    typed_extension_protocol_options: *h1_protocol_options
  - name: base_h2
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http2_protocol_options: {}
    connect_timeout: *connect_timeout
    lb_policy: CLUSTER_PROVIDED
    cluster_type: *base_cluster_type
    transport_socket:
      name: envoy.transport_sockets.http_11_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.http_11_proxy.v3.Http11ProxyUpstreamTransport
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
            common_tls_context:
              alpn_protocols: [h2]
              tls_params:
                tls_maximum_protocol_version: TLSv1_3
              validation_context:
                trusted_ca:
                  inline_string: *tls_root_certs
                trust_chain_verification: *trust_chain_verification
    upstream_connection_options: *upstream_opts
    circuit_breakers: *circuit_breakers_settings
    typed_extension_protocol_options: *h2_protocol_options
  - name: base_h3
    connect_timeout: *connect_timeout
    lb_policy: CLUSTER_PROVIDED
    cluster_type: *base_cluster_type
    transport_socket:
      name: envoy.transport_sockets.http_11_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.http_11_proxy.v3.Http11ProxyUpstreamTransport
        transport_socket:
          name: envoy.transport_sockets.quic
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicUpstreamTransport
            upstream_tls_context:
              common_tls_context:
                tls_params:
                  tls_maximum_protocol_version: TLSv1_3
                validation_context:
                  trusted_ca:
                    inline_string: *tls_root_certs
                  trust_chain_verification: *trust_chain_verification
    upstream_connection_options: *upstream_opts
    circuit_breakers: *circuit_breakers_settings
    typed_extension_protocol_options: *h3_protocol_options
stats_flush_interval: *stats_flush_interval
stats_sinks: *stats_sinks
stats_config:
  stats_matcher:
    inclusion_list:
      patterns:
        - safe_regex:
            regex: '^cluster\.[\w]+?\.upstream_cx_[\w]+'
        - safe_regex:
            regex: '^cluster\.[\w]+?\.upstream_rq_[\w]+'
        - safe_regex:
            regex: '^cluster\.[\w]+?\.update_(attempt|success|failure)'
        - safe_regex:
            regex: '^cluster\.[\w]+?\.http2.keepalive_timeout'
        - safe_regex:
            regex: '^dns.apple.*'
        - safe_regex:
            regex: '^http.client.*'
        - safe_regex:
            regex: '^http.dispatcher.*'
        - safe_regex:
            regex: '^http.hcm.decompressor.*'
        - safe_regex:
            regex: '^http.hcm.downstream_rq_[\w]+'
        - safe_regex:
            regex: '^pbf_filter.*'
        - safe_regex:
            regex: '^pulse.*'
        - safe_regex:
            regex: '^vhost\.[\w]+\.vcluster\.[\w]+?\.upstream_rq_(?:[12345]xx|retry.*|time|timeout|total)'
  use_all_default_tags:
    false
watchdogs:
  main_thread_watchdog:
    megamiss_timeout: 60s
    miss_timeout: 60s
  worker_watchdog:
    megamiss_timeout: 60s
    miss_timeout: 60s
node:
  id: envoy-mobile
  cluster: envoy-mobile
  metadata: *metadata
layered_runtime:
  layers:
    - name: static_layer_0
      static_layer:
        envoy:
          # This disables envoy bug stats, which are filtered out of our stats inclusion list anyway
          # Global stats do not play well with engines with limited lifetimes
          disallow_global_stats: true
          reloadable_features:
            allow_multiple_dns_addresses: *dns_multiple_addresses
            always_use_v6: *force_ipv6
            http2_delay_keepalive_timeout: *h2_delay_keepalive_timeout
)"
// Needed due to warning in
// https://github.com/envoyproxy/envoy/blob/6eb7e642d33f5a55b63c367188f09819925fca34/source/server/server.cc#L546
R"(
        overload:
          global_downstream_max_connections: 0xffffffff # uint32 max
)";
// clang-format on
