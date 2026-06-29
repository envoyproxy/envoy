Added ``dns_cluster_config`` field to ``sub_clusters_config`` in the dynamic forward proxy cluster.
When set, dynamically created sub clusters use the
:ref:`DnsCluster <envoy_v3_api_msg_extensions.clusters.dns.v3.DnsCluster>` extension
(``envoy.cluster.dns``) for DNS configuration, enabling control over refresh rates, failure
backoff, TTL respect, lookup family, and resolver selection. When not set, sub clusters continue
to use legacy ``STRICT_DNS`` discovery and inherit DNS settings from the parent cluster.
