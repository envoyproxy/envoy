Added the ``envoy_dynamic_module_callback_cluster_add_hosts_with_hostnames`` ABI callback so
dynamic-module clusters can assign logical hostnames independently of socket addresses. Upstream
TLS options such as ``auto_host_sni`` and ``auto_sni_san_validation`` can consume the logical
hostname. Null or empty hostnames use the same synthesized hostname behavior as the existing
callback. The Rust SDK exposes convenience and priority/locality-aware methods for the new callback.
