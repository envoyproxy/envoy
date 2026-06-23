Added :ref:`resolved_address_filter
<envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.resolved_address_filter>` to the DNS
cache config. Resolved addresses matching the configured CIDR ranges are removed from DNS responses, which can be
used to prevent the dynamic forward proxy from connecting to internal/private addresses (SSRF protection). Denied
addresses are tracked by the new ``dns_cache.<dns_cache_name>.dns_address_filter_out`` counter.
