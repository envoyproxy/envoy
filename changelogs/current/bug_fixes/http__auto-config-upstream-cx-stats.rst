http: fixed a bug where ``auto_config`` clusters double-counted ``upstream_cx_total`` and
incorrectly reported ``upstream_cx_length_ms`` samples when handing off the established
connection from the initial TCP probe client to the protocol-specific HTTP client. This
behavior can be temporarily reverted by setting the runtime flag
``envoy.reloadable_features.mixed_conn_pool_handover_stats`` to ``false``.
