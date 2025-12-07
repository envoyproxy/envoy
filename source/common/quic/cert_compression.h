#pragma once

#include "source/common/tls/cert_compression.h"

namespace Envoy {
namespace Quic {

/**
 * Backward compatibility wrapper for certificate compression.
 * The implementation has been moved to source/common/tls/cert_compression.h
 * to enable certificate compression for both TCP TLS and QUIC.
 */
class CertCompression : protected Logger::Loggable<Logger::Id::quic> {
public:
  // Registers all compression and decompression functions on `ssl_ctx`.
  // This is a wrapper that calls the TLS implementation.
  // Registers brotli, zstd, and zlib in priority order (RFC 8879).
  static void registerSslContext(SSL_CTX* ssl_ctx) {
    Extensions::TransportSockets::Tls::CertCompression::registerAll(ssl_ctx);
  }

  // Callbacks for `SSL_CTX_add_cert_compression_alg`.
  // These delegate to the TLS implementation.
  static int compressZlib(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len) {
    return Extensions::TransportSockets::Tls::CertCompression::compressZlib(ssl, out, in, in_len);
  }

  static int decompressZlib(SSL* ssl, CRYPTO_BUFFER** out, size_t uncompressed_len,
                            const uint8_t* in, size_t in_len) {
    return Extensions::TransportSockets::Tls::CertCompression::decompressZlib(ssl, out,
                                                                               uncompressed_len, in,
                                                                               in_len);
  }

  // Defined return values for callbacks from `SSL_CTX_add_cert_compression_alg`.
  static constexpr int SUCCESS = Extensions::TransportSockets::Tls::CertCompression::SUCCESS;
  static constexpr int FAILURE = Extensions::TransportSockets::Tls::CertCompression::FAILURE;
};

} // namespace Quic
} // namespace Envoy
