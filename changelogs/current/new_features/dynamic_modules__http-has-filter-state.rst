Added ``envoy_dynamic_module_callback_http_has_filter_state`` ABI callback so a dynamic-module
HTTP filter can check whether a filter state entry exists without reading or serializing the
stored object. The C++, Go, and Rust SDKs expose the new callback.
