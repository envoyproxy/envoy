If tcp_proxy receives data from the upstream while draining the upstream because
the downstream has closed, Envoy closes the connection abortively instead of
gracefully. This behaviour change can be disabled by setting the runtime guard
``envoy.reloadable_features.tcp_proxy_send_rst_on_drain_abort`` to ``false``.
