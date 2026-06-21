Runtime guard ``envoy.reloadable_features.tls_certificate_compression_brotli`` is now disabled by
default. When disabled, QUIC retains zlib-only certificate compression and TCP TLS performs no
certificate compression. It can be re-enabled by setting the runtime guard to ``true``.
