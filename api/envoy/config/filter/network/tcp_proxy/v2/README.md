Protocol buffer definitions for the TCP Proxy.

Deprecated Configuration Example
--------------------------------

Example configuration of deprecated v1 flags for the TCP proxy. Destination IP and port filtering
options should be configured using the filter_chain_match field of the Listener's
[Filter](https://www.envoyproxy.io/docs/envoy/latest/api-v2/api/v2/listener/listener.proto#envoy-api-msg-listener-filter).
Eventually, source IP and port filtering options will be enabled in the same location. Until then,
source filtering can be configured in the TCP Proxy:

``` yaml
- name: "envoy.tcp_proxy"
  config:
    deprecated_v1: true
    value:
      stat_prefix: "prefix"
      route_config:
        routes:
          - cluster: "cluster"
            source_ip_list:
              - "10.1.0.0/16"
              - "2001:db8::/32"
            source_ports: "8000,9000-9999"
```

The `source_ip_list` field is a list of strings containing CIDR range specifiers. The
`source_ports` field is a single string containing comma-separated ports and/or port ranges.
