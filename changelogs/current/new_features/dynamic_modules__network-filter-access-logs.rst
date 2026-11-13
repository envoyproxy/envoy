Added ``access_log`` and ``access_log_options`` to the dynamic module network filter. The filter
flushes the access logs when the connection closes, and also on a new connection or at an interval
if set in ``access_log_options``. It records when the connection ends and gives the connection a
stream ID if it has none. Access logs are not supported on QUIC listeners. A module can also flush
them with ``flush_access_log``. A terminal module that reaches its upstream itself can report the
upstream, the timing, the bytes and the outcome of the connection with ``set_upstream_host``,
``set_upstream_cluster``, ``set_upstream_local_address``, ``set_upstream_transport_failure_reason``,
``set_upstream_local_close_reason``, ``set_upstream_detected_close_type``,
``set_upstream_request_attempt_count``, ``record_upstream_timing``, ``add_upstream_wire_bytes``,
``add_downstream_wire_bytes``, ``set_response_flag`` and ``add_custom_flag``, so that access logs
show them as they do for the TCP proxy. The Rust SDK exposes all of these callbacks as methods of
``EnvoyNetworkFilter`` with the same names.
