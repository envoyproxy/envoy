Added the ``envoy_dynamic_module_callback_network_filter_start_downstream_secure_transport`` ABI
callback so a dynamic-module network filter can promote its downstream connection to TLS. The Rust
SDK exposes this as ``EnvoyNetworkFilter::start_downstream_secure_transport``.
