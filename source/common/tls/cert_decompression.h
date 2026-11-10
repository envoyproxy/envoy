#pragma once

#include "source/common/common/logger.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// RFC 8879 TLS Certificate Compression: the decompression half, which is all a TLS client needs.
//
// BoringSSL advertises an algorithm in the `compress_certificate` extension based only on its
// decompression callback, and only compresses its own certificate when acting as a server, so a
// client context that registers decompression only is identical on the wire to one that registers
// both. This library does not depend on any compressor, so client-only binaries (e.g. Envoy Mobile)
// that use it do not link the certificate compressors (e.g. the brotli encoder). Server contexts
// use `CertCompression` (cert_compression.h), which registers both.
class CertDecompression : protected Logger::Loggable<Logger::Id::connection> {
public:
  // Register decompression only (the compression callback is null).
  static void registerBrotli(SSL_CTX* ssl_ctx);
  static void registerZlib(SSL_CTX* ssl_ctx);

  static int decompressBrotli(SSL* ssl, CRYPTO_BUFFER** out, size_t uncompressed_len,
                              const uint8_t* in, size_t in_len);
  static int decompressZlib(SSL* ssl, CRYPTO_BUFFER** out, size_t uncompressed_len,
                            const uint8_t* in, size_t in_len);

  static constexpr int SUCCESS = 1;
  static constexpr int FAILURE = 0;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
