QUIC upstream TLS handshakes now honor configured ``ecdh_curves`` on the client ``SSL_CTX``,
including the default selected by
``envoy.reloadable_features.pqc_default_ecdh_curves``, matching TCP TLS behavior.
