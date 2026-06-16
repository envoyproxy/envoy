Added support for per-certificate TLS parameter overrides via the ``tls_params`` field on
:ref:`TlsCertificate <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.TlsCertificate>`.
When set, these parameters override the context-level TLS configuration entirely for that
certificate's SSL context, allowing different cipher suites, ECDH curves, protocol versions,
and signature algorithms per certificate.
