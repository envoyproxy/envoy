Fixed QUIC connection info to serve peer certificate details through the same code path as TLS
connections. The peer certificate issuer digest and issuer serial number (used for example by the
Lua filter) and the raw PEM-encoded peer certificate (used for example by CEL attributes) are now
populated on QUIC connections, and peer certificate details remain available after the SSL object
has been released when ``envoy.reloadable_features.quic_enable_reset_ssl_after_handshake`` is
enabled.
