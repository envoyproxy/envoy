Added optional ciphertext read-ahead for TCP TLS sockets after the handshake to reduce socket read
calls. Configure a nonzero
:ref:`read_ahead_buffer_size <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CommonTlsContext.read_ahead_buffer_size>`
to enable it; the default is ``0`` (disabled). The buffer is allocated on the first read after the
handshake and is separate from the connection's plaintext buffer limits. Read-ahead can be temporarily
disabled for new connections by setting the runtime guard
``envoy.reloadable_features.tls_io_handle_read_ahead`` to ``false``.
