Added HTTP filter dynamic module callback ``envoy_dynamic_module_callback_http_drain_connection_upon_completion``
and corresponding C++, Go, and Rust SDK methods to initiate graceful draining of the downstream connection when
the current HTTP stream completes.
