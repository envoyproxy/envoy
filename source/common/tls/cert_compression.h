#pragma once

#include "source/common/common/logger.h"
#include "source/common/tls/cert_decompression.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// RFC 8879 TLS Certificate Compression: compression and decompression, for server contexts, which
// send (and may compress) their own certificate chain. Client contexts only need decompression;
// see `CertDecompression` (cert_decompression.h), which does not depend on the compressors.
class CertCompression : protected Logger::Loggable<Logger::Id::connection> {
public:
  // Register compression and decompression.
  static void registerBrotli(SSL_CTX* ssl_ctx);
  static void registerZlib(SSL_CTX* ssl_ctx);

  static int compressBrotli(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len);
  static int compressZlib(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len);

  static constexpr int SUCCESS = CertDecompression::SUCCESS;
  static constexpr int FAILURE = CertDecompression::FAILURE;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
