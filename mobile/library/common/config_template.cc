/**
 * Templated default configuration
 */
const char* platform_filter_template = R"(
          - name: envoy.filters.http.platform_bridge
            typed_config:
              "@type": type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge
              platform_filter_name: {{ platform_filter_name }}
)";

const char* config_template = R"(
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address:
        protocol: TCP
        address: 0.0.0.0
        port_value: 10000
    api_listener:
      api_listener:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
        stat_prefix: hcm
        route_config:
          name: api_router
          virtual_hosts:
            - name: api
              include_attempt_count_in_response: true
              virtual_clusters: {{ virtual_clusters }}
              domains:
                - "*"
              routes:
                - match:
                    prefix: "/"
                  route:
                    cluster_header: x-envoy-mobile-cluster
                    retry_policy:
                      retry_back_off:
                        base_interval: 0.25s
                        max_interval: 60s
        http_filters:
{{ platform_filter_chain }}
          - name: envoy.filters.http.dynamic_forward_proxy
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
              dns_cache_config: &dns_cache_config
                name: dynamic_forward_proxy_cache_config
                # TODO: Support IPV6 https://github.com/lyft/envoy-mobile/issues/1022
                dns_lookup_family: V4_ONLY
                dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
                dns_failure_refresh_rate:
                  base_interval: {{ dns_failure_refresh_rate_seconds_base }}s
                  max_interval: {{ dns_failure_refresh_rate_seconds_max }}s
          # TODO: make this configurable for users.
          - name: envoy.filters.http.decompressor
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.decompressor.v3.Decompressor
              decompressor_library:
                name: gzip
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip
                  # Maximum window bits to allow for any stream to be decompressed. Optimally this
                  # would be set to 0. According to the zlib manual this would allow the decompressor
                  # to use the window bits in the zlib header to perform the decompression.
                  # Unfortunately, the proto field constraint makes this impossible currently.
                  window_bits: 15
              request_direction_config:
                common_config:
                  enabled:
                    default_value: false
                    runtime_key: request_decompressor_enabled
          - name: envoy.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  clusters:
  - name: base
    connect_timeout: {{ connect_timeout_seconds }}s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config: *dns_cache_config
    transport_socket: &base_transport_socket
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
        common_tls_context:
          validation_context:
            trusted_ca:
              inline_string: |
)"
#include "certificates.inc"

                              R"(
    upstream_connection_options: &upstream_opts
      tcp_keepalive:
        keepalive_interval: 10
        keepalive_probes: 1
        keepalive_time: 5
    circuit_breakers: &circuit_breakers_settings
      thresholds:
        - priority: DEFAULT
          # n.b: with mobile clients there are scenarios where all concurrent requests might be
          # retries (e.g., when the phone goes offline). Therefore, Envoy Mobile allows for the same
          # amount of concurrency for retries as the default value for concurrency for requests.
          # This configuration uses the default for max concurrent requests (1024) because there is
          # no reasonable scenario where a mobile client should have even close to that many concurrent
          # requests.
          #
          # `max_retries` could be used but maintainers advised against it, as there are plans to
          # deprecate that setting.
          # https://github.com/lyft/envoy-mobile/pull/811#issuecomment-619169529.
          retry_budget:
            budget_percent:
              value: 100
            min_retry_concurrency: 1024
  - name: base_wlan
    connect_timeout: {{ connect_timeout_seconds }}s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config: *dns_cache_config
    transport_socket: *base_transport_socket
    upstream_connection_options: *upstream_opts
    circuit_breakers: *circuit_breakers_settings
  - name: base_wwan
    connect_timeout: {{ connect_timeout_seconds }}s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config: *dns_cache_config
    transport_socket: *base_transport_socket
    upstream_connection_options: *upstream_opts
    circuit_breakers: *circuit_breakers_settings
  - name: base_h2
    http2_protocol_options: {}
    connect_timeout: {{ connect_timeout_seconds }}s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config: *dns_cache_config
    transport_socket: *base_transport_socket
    upstream_connection_options: *upstream_opts
    circuit_breakers: *circuit_breakers_settings
  - name: base_wlan_h2
    http2_protocol_options: {}
    connect_timeout: {{ connect_timeout_seconds }}s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config: *dns_cache_config
    transport_socket: *base_transport_socket
    upstream_connection_options: *upstream_opts
    circuit_breakers: *circuit_breakers_settings
  - name: base_wwan_h2
    http2_protocol_options: {}
    connect_timeout: {{ connect_timeout_seconds }}s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config: *dns_cache_config
    transport_socket: *base_transport_socket
    upstream_connection_options: *upstream_opts
    circuit_breakers: *circuit_breakers_settings
  - name: stats
    connect_timeout: {{ connect_timeout_seconds }}s
    dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
    http2_protocol_options: {}
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: stats
      endpoints:
        - lb_endpoints:
            - endpoint:
                address:
                  socket_address: {address: {{ stats_domain }}, port_value: 443}
    transport_socket: *base_transport_socket
    type: LOGICAL_DNS
stats_flush_interval: {{ stats_flush_interval_seconds }}s
stats_sinks:
  - name: envoy.metrics_service
    typed_config:
      "@type": type.googleapis.com/envoy.config.metrics.v3.MetricsServiceConfig
      report_counters_as_deltas: true
      grpc_service:
        envoy_grpc:
          cluster_name: stats
stats_config:
  stats_matcher:
    inclusion_list:
      patterns:
        - safe_regex:
            google_re2: {}
            regex: '^cluster\.[\w]+?\.upstream_cx_[\w]+'
        - safe_regex:
            google_re2: {}
            regex: '^cluster\.[\w]+?\.upstream_rq_[\w]+'
        - safe_regex:
            google_re2: {}
            regex: '^http.dispatcher.*'
        - safe_regex:
            google_re2: {}
            regex: '^client.*'
        - safe_regex:
            google_re2: {}
            regex: '^http.hcm.decompressor.*'
        - safe_regex:
            google_re2: {}
            regex: '^http.hcm.downstream_rq_(?:[12345]xx|total|completed)'
        - safe_regex:
            google_re2: {}
            regex: '^vhost.api.vcluster\.[\w]+?\.upstream_rq_(?:[12345]xx|retry.*|time|timeout|total)'
watchdog:
  megamiss_timeout: 60s
  miss_timeout: 60s
node:
  metadata:
    app_id : {{ app_id }}
    app_version : {{ app_version }}
    os: {{ device_os }}
# Needed due to warning in https://github.com/envoyproxy/envoy/blob/6eb7e642d33f5a55b63c367188f09819925fca34/source/server/server.cc#L546
layered_runtime:
  layers:
    - name: static_layer_0
      static_layer:
        overload:
          global_downstream_max_connections: 50000
)";
