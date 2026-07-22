Added support for per-certificate TLS parameter overrides via the ``tls_params`` field on
:ref:`TlsCertificate <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.TlsCertificate>`.
When set on a server certificate, any specified fields override the corresponding context-level
TLS parameters for that certificate during the TLS handshake; unset fields continue to use the
context-level values. This allows different cipher suites, ECDH curves, protocol versions,
signature algorithms, and compliance policies per certificate. This field has no effect on client
certificates.
