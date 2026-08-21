Added support for client certificate authentication (mTLS) on QUIC/HTTP3 listeners. When
:ref:`require_client_certificate
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.DownstreamTlsContext.require_client_certificate>`
is enabled in the QUIC downstream TLS context (previously rejected at configuration load time),
the server requests a client certificate during the TLS handshake and validates it against the
configured validation context, including asynchronous certificate validators.
