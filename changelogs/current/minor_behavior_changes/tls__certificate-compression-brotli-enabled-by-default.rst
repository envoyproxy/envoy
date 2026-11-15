Runtime guard ``envoy.reloadable_features.tls_certificate_compression_brotli`` is enabled by
default again, now that compressed certificate chains are cached per TLS context instead of
being recompressed on every handshake. QUIC and TCP TLS both offer brotli and zlib certificate
compression (RFC 8879). It can be disabled by setting the runtime guard to ``false``, in which case
QUIC offers only zlib and TCP TLS performs no certificate compression.
