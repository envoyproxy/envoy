Added the ``envoy_dynamic_module_callback_log_v2`` callback, which logs a message and reports
the module source location of the log statement instead of a location inside Envoy. The Rust, Go
and C++ SDKs capture the call site automatically.
