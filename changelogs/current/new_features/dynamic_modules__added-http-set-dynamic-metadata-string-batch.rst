Added ``envoy_dynamic_module_callback_http_set_dynamic_metadata_string_batch`` ABI callback that
sets multiple string-valued dynamic metadata entries under a single namespace in one call,
resolving the namespace and merging into the metadata struct once instead of once per entry. The
Rust SDK exposes this as ``EnvoyHttpFilter::set_dynamic_metadata_string_batch``.
