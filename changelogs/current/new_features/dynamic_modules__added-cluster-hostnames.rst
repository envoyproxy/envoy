Added the ``envoy_dynamic_module_callback_cluster_add_hosts_with_hostnames`` ABI callback so
dynamic-module clusters can assign logical hostnames independently of socket addresses. Upstream
features such as automatic SNI and SAN validation can consume the logical hostname. Null or empty
hostnames preserve the legacy synthesized hostname behavior. The Rust SDK exposes convenience and
priority/locality-aware methods for the new callback.
