dynamic modules: added :ref:`use_persistent_host_partitions
<envoy_v3_api_field_extensions.clusters.dynamic_modules.v3.ClusterConfig.use_persistent_host_partitions>`
to the dynamic modules cluster. When set, adding, removing, or changing the health of hosts updates
only those hosts instead of rebuilding the host set, and workers adopt the result as a snapshot.
