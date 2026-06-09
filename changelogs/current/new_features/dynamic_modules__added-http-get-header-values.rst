Added ``envoy_dynamic_module_callback_http_get_header_values`` ABI callback that returns all values
of a header in a single call, replacing the per-value loop that crossed the ABI boundary and looked
up the header once per value. The Rust SDK uses this for
``EnvoyHttpFilter::get_request_header_values`` and the response, request trailer, and response
trailer variants.
