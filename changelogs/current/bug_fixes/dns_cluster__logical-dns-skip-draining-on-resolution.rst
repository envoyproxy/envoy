Fixed a regression in the new DNS cluster implementation
(``envoy.reloadable_features.enable_new_dns_implementation``) where a logical DNS cluster replaced
its host whenever the resolved addresses changed, draining all connection pools to the host on
every change. This caused heavy connection churn for DNS servers that rotate their answers, e.g.
for load balancing. The host's addresses are now updated in place, as the legacy logical DNS
cluster did. The previous replace-and-drain behavior can be restored by setting the new
:ref:`skip_draining_on_resolution <envoy_v3_api_field_extensions.clusters.dns.v3.DnsCluster.skip_draining_on_resolution>`
field to ``false``.
