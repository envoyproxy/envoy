TCP TLS sockets now read ahead ciphertext after the handshake to reduce socket read calls. The read
size follows the connection's plaintext read reservation with an allowance for TLS record overhead.
The ciphertext buffer is allocated on demand, reused while data remains available, and released once
empty when a socket read would block, reaches the end of the stream, or reports a non-retryable error.
Ciphertext storage is separate from the connection's plaintext buffer limits. This behavior can be
temporarily reverted for new connections by setting runtime guard
``envoy.reloadable_features.tls_io_handle_read_ahead`` to
``false``.
