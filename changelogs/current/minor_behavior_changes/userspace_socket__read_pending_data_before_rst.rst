If an abortive connection closure is requested while there is pending data in
the receive queue, subsequent calls to read() will read the pending data before
delivering ECONNRESET. This behaviour is guarded by the existing runtime guard
``envoy.reloadable_features.tcp_proxy_send_rst_on_drain_abort`` and can be
disabled by setting the runtime guard to ``false``.
