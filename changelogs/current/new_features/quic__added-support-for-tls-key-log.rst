Added support for TLS key logging in QUIC via :ref:`key_log <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CommonTlsContext.key_log>`,
applying the same local/remote IP-list filtering as TCP TLS key log. When ``key_log`` is configured
and runtime guard ``envoy.restart_features.quic_keylog_support`` is enabled, Envoy writes NSS key log
lines for QUIC connections so captured QUIC traffic can be decrypted for debugging. The runtime guard
defaults to ``false``.
