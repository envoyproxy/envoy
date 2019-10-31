/**
 * Templated default configuration
 */
const char* config_template = R"(
static_resources:
  clusters:
  - name: base # Note: the direct API depends on the existence of a cluster with this name.
    connect_timeout: {{ connect_timeout_seconds }}s
    dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
    dns_lookup_family: V4_ONLY
    http2_protocol_options: {}
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: base
      endpoints: &base_endpoints
        - lb_endpoints:
            - endpoint:
                address:
                  socket_address: {address: {{ domain }}, port_value: 443}
    tls_context: &base_tls_context
      common_tls_context:
        validation_context:
          trusted_ca:
            inline_string: |
)"
#include "certificates.inc"
R"(
          verify_subject_alt_name:
            - {{ domain }}
      sni: {{ domain }}
    type: LOGICAL_DNS
  - name: base_wlan # Note: the direct API depends on the existence of a cluster with this name.
    connect_timeout: {{ connect_timeout_seconds }}s
    dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
    dns_lookup_family: V4_ONLY
    http2_protocol_options: {}
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: base_wlan
      endpoints: *base_endpoints
    tls_context: *base_tls_context
    type: LOGICAL_DNS
  - name: base_wwan # Note: the direct API depends on the existence of a cluster with this name.
    connect_timeout: {{ connect_timeout_seconds }}s
    dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
    dns_lookup_family: V4_ONLY
    http2_protocol_options: {}
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: base_wwan
      endpoints: *base_endpoints
    tls_context: *base_tls_context
    type: LOGICAL_DNS
  - name: stats
    connect_timeout: {{ connect_timeout_seconds }}s
    dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
    dns_lookup_family: V4_ONLY
    http2_protocol_options: {}
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: stats
      endpoints:
        - lb_endpoints:
            - endpoint:
                address:
                  socket_address: {address: {{ stats_domain }}, port_value: 443}
    tls_context: *base_tls_context
    type: LOGICAL_DNS
stats_flush_interval: {{ stats_flush_interval_seconds }}s
stats_sinks:
  - name: envoy.metrics_service
    config:
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
