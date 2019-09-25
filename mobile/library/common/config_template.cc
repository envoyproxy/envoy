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
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: base
      endpoints:
        - lb_endpoints:
            - endpoint:
                address:
                  socket_address: {address: {{ domain }}, port_value: 443}
    tls_context:
      common_tls_context:
        validation_context:
          trusted_ca: &trusted_ca
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
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: base_wlan
      endpoints:
        - lb_endpoints:
            - endpoint:
                address:
                  socket_address: {address: {{ domain }}, port_value: 443}
    tls_context:
      common_tls_context:
        validation_context:
          trusted_ca: *trusted_ca
          verify_subject_alt_name:
            - {{ domain }}
      sni: {{ domain }}
    type: LOGICAL_DNS
  - name: base_wwan # Note: the direct API depends on the existence of a cluster with this name.
    connect_timeout: {{ connect_timeout_seconds }}s
    dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
    dns_lookup_family: V4_ONLY
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: base_wwan
      endpoints:
        - lb_endpoints:
            - endpoint:
                address:
                  socket_address: {address: {{ domain }}, port_value: 443}
    tls_context:
      common_tls_context:
        validation_context:
          trusted_ca: *trusted_ca
          verify_subject_alt_name:
            - {{ domain }}
      sni: {{ domain }}
    type: LOGICAL_DNS
stats_flush_interval: {{ stats_flush_interval_seconds }}s
watchdog:
  megamiss_timeout: 60s
  miss_timeout: 60s
)";
