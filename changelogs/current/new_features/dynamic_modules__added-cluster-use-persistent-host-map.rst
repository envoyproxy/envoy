Added the ``envoy_dynamic_module_callback_cluster_use_persistent_host_map`` cluster ABI callback and
the Rust SDK ``EnvoyCluster::use_persistent_host_map`` method, which back the cross-priority host
map of a dynamic modules cluster with a persistent map so that each host update costs O(log N)
instead of a full copy of the map. The default flat map is unchanged.
