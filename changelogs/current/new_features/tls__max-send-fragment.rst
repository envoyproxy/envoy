Added :ref:`max_send_fragment
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.CommonTlsContext.max_send_fragment>` to
``CommonTlsContext``. It caps the plaintext size of every TLS record Envoy sends, handshake messages
included, for peers that cannot handle full 16384-byte records, such as some PostgreSQL clients.
