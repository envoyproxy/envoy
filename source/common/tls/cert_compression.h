#pragma once

#include "source/common/common/logger.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

/**
 * Support for certificate compression and decompression in TLS handshakes.
 * Certificate compression reduces the size of TLS handshakes, which is especially
 * important for QUIC where the ServerHello needs to fit in the initial response,
 * and beneficial for TCP TLS to reduce bandwidth overhead.
 */
class CertCompression : protected Logger::Loggable<Logger::Id::connection> {
public:
  // Registers zlib compression and decompression functions on `ssl_ctx`.
  static void registerZlib(SSL_CTX* ssl_ctx);

  // Callbacks for `SSL_CTX_add_cert_compression_alg`.
  static int compressZlib(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len);
  static int decompressZlib(SSL* ssl, CRYPTO_BUFFER** out, size_t uncompressed_len,
                            const uint8_t* in, size_t in_len);

  // Defined return values for callbacks from `SSL_CTX_add_cert_compression_alg`.
  static constexpr int SUCCESS = 1;
  static constexpr int FAILURE = 0;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
