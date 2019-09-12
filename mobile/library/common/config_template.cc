// NOLINT(namespace-envoy)

const char* config_template = R"(
static_resources:
  listeners:
  - address:
      socket_address: {address: 0.0.0.0, port_value: 9000, protocol: TCP}
    filter_chains:
    - filters:
      - name: envoy.http_connection_manager
        config:
          stat_prefix: base
          route_config:
            virtual_hosts:
            - name: all
              domains: ["*"]
              routes:
                - match: {prefix: "/"}
                  route: {cluster: base}
          http_filters:
          - name: envoy.router
  - address:
      socket_address:
        protocol: TCP
        address: 0.0.0.0
        port_value: 9001
    filter_chains:
    - filters:
      - name: envoy.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
          stat_prefix: client
          http_protocol_options:
            allow_absolute_url: true
          route_config:
            name: local_route
            virtual_hosts:
            - name: all
              domains: ["*"]
              routes:
              - match:
                  prefix: "/"
                route:
                  cluster: default_egress
          http_filters:
          - name: envoy.filters.http.dynamic_forward_proxy
            config:
              dns_cache_config:
                name: dynamic_forward_proxy_cache_config
                dns_lookup_family: AUTO
          - name: envoy.router
  clusters:
  - name: base
    connect_timeout: {{ connect_timeout }}
    dns_refresh_rate: {{ dns_refresh_rate }}
    dns_lookup_family: V4_ONLY
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: base
      endpoints:
        - lb_endpoints:
            - endpoint:
                address:
                  socket_address: {address: {{ domain }}, port_value: 443}
    tls_context:
      sni: {{ domain }}
    type: LOGICAL_DNS
  - name: default_egress
    connect_timeout: {{ connect_timeout }}
    dns_refresh_rate: {{ dns_refresh_rate }}
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig
        dns_cache_config:
          name: dynamic_forward_proxy_cache_config
          dns_lookup_family: AUTO
    # WARNING!
    # TODO: Enable TLS in https://github.com/lyft/envoy-mobile/issues/322
    #
    # tls_context:
    #   common_tls_context:
    #     validation_context:
    #       trusted_ca: {filename: /etc/ssl/certs/ca-certificates.crt}
stats_flush_interval: {{ stats_flush_interval }}
watchdog:
  megamiss_timeout: 60s
  miss_timeout: 60s
)";
