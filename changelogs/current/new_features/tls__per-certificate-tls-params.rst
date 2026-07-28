Added support for per-certificate TLS parameter overrides via the ``tls_params`` field on
:ref:`TlsCertificate <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.TlsCertificate>`.
When set on a server certificate, any specified fields override the corresponding context-level
TLS parameters for that certificate during the TLS handshake; unset fields continue to use the
context-level values. This allows different cipher suites, ECDH curves, protocol versions,
signature algorithms, and compliance policies per certificate. These parameters do not affect
certificate selection, which remains based on SNI and the client's ECDSA and OCSP capability, and
are applied after a certificate has been selected. With multiple certificates, each certificate's
parameters must be compatible with the clients that select it, since an incompatible selected
certificate fails the handshake rather than falling back to another certificate. This field is not
supported on client certificates and is ignored. It also has no effect on QUIC/HTTP3 downstream
connections.
