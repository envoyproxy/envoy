#pragma once

#include "source/common/common/logger.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Quic {

/**
 * Support for certificate compression and decompression in QUIC TLS handshakes. This often
 * needed for the ServerHello to fit in the initial response and not need an additional round trip
 * between client and server.
 */
class CertCompression : protected Logger::Loggable<Logger::Id::quic> {
public:
  // Registers compression and decompression functions on `ssl_ctx` if enabled.
  static void registerSslContext(SSL_CTX* ssl_ctx);

  // Callbacks for `SSL_CTX_add_cert_compression_alg`.
  static int compressZlib(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len);
  static int decompressZlib(SSL*, CRYPTO_BUFFER** out, size_t uncompressed_len, const uint8_t* in,
                            size_t in_len);

  // Defined return values for callbacks from `SSL_CTX_add_cert_compression_alg`.
  static constexpr int SUCCESS = 1;
  static constexpr int FAILURE = 0;
};

} // namespace Quic
} // namespace Envoy
