Added an optional ``max_cleartext_read_buffer_size`` to the downstream StartTLS transport socket.
This allows protocol filters to bound a cleartext read before switching to TLS.
