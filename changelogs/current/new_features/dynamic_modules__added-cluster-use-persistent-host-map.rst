Added the ``envoy_dynamic_module_callback_cluster_use_persistent_host_map`` ABI callback so that a
dynamic-modules cluster can opt into a persistent cross-priority host map in place of the
default flat copy-on-write map, making each membership delta O(delta) instead of an O(N) flat copy.
Membership behavior is identical under either backing, and the flat map remains the default. The
Rust SDK exposes this as ``EnvoyCluster::set_use_persistent_host_map``.
