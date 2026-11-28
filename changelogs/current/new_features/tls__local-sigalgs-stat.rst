Added the ``ssl.local_sigalgs.<sigalg>`` counter to the :ref:`TLS statistics <config_listener_stats_tls>`,
which counts the signature algorithm Envoy used to sign the handshake with its own certificate.
Unlike the cipher suite, it tells which of several configured certificates (for example RSA and
ECDSA) a TLS 1.3 handshake served.
