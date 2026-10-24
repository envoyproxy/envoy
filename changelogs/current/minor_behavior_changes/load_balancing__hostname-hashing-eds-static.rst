Allowed :ref:`use_hostname_for_hashing
<envoy_v3_api_field_config.cluster.v3.Cluster.CommonLbConfig.ConsistentHashingLbConfig.use_hostname_for_hashing>`
for ``STATIC`` and ``EDS`` clusters in addition to ``STRICT_DNS`` clusters. The optional
:ref:`hostname <envoy_v3_api_field_config.endpoint.v3.Endpoint.hostname>` field of the endpoint is
used as the hash key, and hosts without a hostname fall back to hashing by the resolved address.
