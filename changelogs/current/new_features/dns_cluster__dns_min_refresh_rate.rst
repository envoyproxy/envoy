Added ``dns_min_refresh_rate`` field to
:ref:`DnsCluster <envoy_v3_api_msg_extensions.clusters.dns.v3.DnsCluster>`.
When :ref:`respect_dns_ttl <envoy_v3_api_field_extensions.clusters.dns.v3.DnsCluster.respect_dns_ttl>`
is enabled, this sets a minimum floor on the TTL-derived refresh interval. DNS records with TTLs
shorter than this value will be refreshed at this rate instead, preventing excessively frequent
re-resolution for low-TTL records.
