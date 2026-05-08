#pragma once

#include "source/common/common/logger.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// RFC 8879 TLS Certificate Compression.
class CertCompression : protected Logger::Loggable<Logger::Id::connection> {
public:
  static void registerBrotli(SSL_CTX* ssl_ctx);
  static void registerZlib(SSL_CTX* ssl_ctx);

  static int compressBrotli(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len);
  static int decompressBrotli(SSL* ssl, CRYPTO_BUFFER** out, size_t uncompressed_len,
                              const uint8_t* in, size_t in_len);

  static int compressZlib(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len);
  static int decompressZlib(SSL* ssl, CRYPTO_BUFFER** out, size_t uncompressed_len,
                            const uint8_t* in, size_t in_len);

  static constexpr int SUCCESS = 1;
  static constexpr int FAILURE = 0;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
