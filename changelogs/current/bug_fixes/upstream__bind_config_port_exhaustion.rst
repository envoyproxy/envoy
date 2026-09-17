Fixed a bug where ``upstream_bind_config`` with port ``0`` could cause ephemeral
port exhaustion by reserving an ephemeral port during ``bind()``. Envoy now
automatically enables ``IP_BIND_ADDRESS_NO_PORT`` to defer port allocation until
``connect()``. This change can be temporarily reverted by setting runtime guard
``envoy.reloadable_features.upstream_bind_config_fix_port_exhaustion`` to ``false``.
