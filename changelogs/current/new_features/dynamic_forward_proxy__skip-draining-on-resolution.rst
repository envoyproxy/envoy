Added :ref:`skip_draining_on_resolution
<envoy_v3_api_field_extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig.skip_draining_on_resolution>`
to the dynamic forward proxy cluster. It defaults to ``true``, which keeps the existing behavior of
updating a host's address in place when DNS resolution changes it. When set to ``false``, the host
is replaced by a new host and the old host is published as removed, so connection pools to
addresses that are no longer resolved are drained.
