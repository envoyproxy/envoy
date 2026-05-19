``SslSocket`` now reports ``ECONNRESET`` as ``ConnectionReset`` by reading the system error
code from BoringSSL's error queue, matching ``RawBufferSocket`` behavior. When a connection
is closed with ``ConnectionCloseType::AbortReset``, ``SslSocket`` also skips the TLS
``close_notify`` shutdown so the peer reliably observes a TCP RST instead of racing a
graceful close against the reset.
This behavioral change can be temporarily reverted by setting runtime guard
``envoy.reloadable_features.ssl_socket_report_connection_reset`` to ``false``.
