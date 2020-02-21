/**
 * Templated default configuration
 */
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
          - name: envoy.filters.http.dynamic_forward_proxy
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
              dns_cache_config:
                name: dynamic_forward_proxy_cache_config
                dns_lookup_family: AUTO
                dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
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
        dns_cache_config:
          name: dynamic_forward_proxy_cache_config
          dns_lookup_family: AUTO
          dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
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
    transport_socket: *base_transport_socket
    upstream_connection_options: *upstream_opts
  - name: base_wlan
    connect_timeout: {{ connect_timeout_seconds }}s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config:
          name: dynamic_forward_proxy_cache_config
          dns_lookup_family: AUTO
          dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
    transport_socket: *base_transport_socket
    upstream_connection_options: *upstream_opts
  - name: base_wwan
    connect_timeout: {{ connect_timeout_seconds }}s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config:
          name: dynamic_forward_proxy_cache_config
          dns_lookup_family: AUTO
          dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
    transport_socket: *base_transport_socket
    upstream_connection_options: *upstream_opts
  - name: base_h2
    http2_protocol_options: {}
    connect_timeout: {{ connect_timeout_seconds }}s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config:
          name: dynamic_forward_proxy_cache_config
          dns_lookup_family: AUTO
          dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
    transport_socket: *base_transport_socket
    upstream_connection_options: *upstream_opts
  - name: base_wlan_h2
    http2_protocol_options: {}
    connect_timeout: {{ connect_timeout_seconds }}s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config:
          name: dynamic_forward_proxy_cache_config
          dns_lookup_family: AUTO
          dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
    transport_socket: *base_transport_socket
    upstream_connection_options: *upstream_opts
  - name: base_wwan_h2
    http2_protocol_options: {}
    connect_timeout: {{ connect_timeout_seconds }}s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config:
          name: dynamic_forward_proxy_cache_config
          dns_lookup_family: AUTO
          dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
    transport_socket: *base_transport_socket
    upstream_connection_options: *upstream_opts
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
      grpc_service:
        envoy_grpc:
          cluster_name: stats
stats_config:
  stats_matcher:
    inclusion_list:
      patterns:
        - safe_regex:
            google_re2: {}
            regex: 'cluster\.[\w]+?\.upstream_rq_total'
        - safe_regex:
            google_re2: {}
            regex: 'cluster\.[\w]+?\.upstream_cx_active'
        - safe_regex:
            google_re2: {}
            regex: 'cluster\.[\w]+?\.upstream_rq_time'
watchdog:
  megamiss_timeout: 60s
  miss_timeout: 60s
node:
  metadata:
    os: {{ device_os }}
)";
