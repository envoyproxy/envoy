TLS client contexts, for both TCP and QUIC, now register RFC 8879 certificate decompression only.
Before, they also registered the certificate compressors, which a TLS client never uses. The
``compress_certificate`` extension a client sends is unchanged, so this has no effect on the wire.
It removes the brotli encoder from client-only builds such as Envoy Mobile (about 600 KB of
uncompressed arm64 code and data). As part of this change, the certificate decompressors moved to
the new ``//source/common/tls:cert_decompression_lib``. The QUIC server's registration, previously
``Quic::registerCertCompression()``, is no longer part of ``envoy_quic_utils.h``. The
``ContextImpl`` constructor now takes the certificate compression registration function from its
subclass.
