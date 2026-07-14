Added ``envoy_dynamic_module_callback_get_log_level`` ABI callback that returns the current effective
log level of the dynamic modules logging stream. Exposed in the Rust SDK as ``get_log_level`` (with
``is_log_enabled`` to check a specific level) and on the Go HTTP filter and filter config handles as
``GetLogLevel`` and ``IsLogLevelEnabled``.
