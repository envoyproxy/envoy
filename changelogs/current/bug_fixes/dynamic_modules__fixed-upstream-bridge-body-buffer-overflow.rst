Fixed a buffer overflow in the upstream HTTP/TCP bridge body getters where a request or response
body split into more than 64 buffer slices would write past the fixed-size array that the Rust SDK
passed to ``envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer`` and its
response counterpart. Added the
``envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer_chunks_size`` and
``envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer_chunks_size`` ABI
callbacks so the module sizes the slice array before filling it.
