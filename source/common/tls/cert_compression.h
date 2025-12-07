#pragma once

#include <vector>

#include "envoy/ssl/context_config.h"

#include "source/common/common/logger.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// RFC 8879 Certificate Compression Algorithm IDs.
// TLSEXT_cert_compression_zlib (1) is defined in BoringSSL.
// Define brotli (2) and zstd (3) locally as they may not be in all BoringSSL versions.
#ifndef TLSEXT_cert_compression_brotli
#define TLSEXT_cert_compression_brotli 2
#endif
#ifndef TLSEXT_cert_compression_zstd
#define TLSEXT_cert_compression_zstd 3
#endif

/**
 * Support for certificate compression and decompression in TLS handshakes.
 * Certificate compression reduces the size of TLS handshakes, which is especially
 * important for QUIC where the ServerHello needs to fit in the initial response,
 * and beneficial for TCP TLS to reduce bandwidth overhead.
 *
 * Supported algorithms (RFC 8879):
 * - zlib (algorithm ID 1): Standard deflate compression
 * - brotli (algorithm ID 2): Better compression ratio, widely supported
 * - zstd (algorithm ID 3): Fast compression with good ratio
 */
class CertCompression : protected Logger::Loggable<Logger::Id::connection> {
public:
  // Register compression algorithms from configuration.
  // Only registers the algorithms specified in the config, in the order specified.
  // The order determines priority - first algorithm in the list that the peer supports is used.
  // If the config is empty, no compression algorithms are registered.
  static void registerFromConfig(
      SSL_CTX* ssl_ctx,
      const std::vector<Ssl::CertificateCompressionAlgorithmConfig>& algorithms);

  // Register all supported compression algorithms in priority order.
  // Priority: brotli > zstd > zlib (brotli generally provides best compression for certs)
  static void registerAll(SSL_CTX* ssl_ctx);

  // Individual registration functions for each algorithm.
  static void registerBrotli(SSL_CTX* ssl_ctx);
  static void registerZstd(SSL_CTX* ssl_ctx);
  static void registerZlib(SSL_CTX* ssl_ctx);

  // Brotli compression callbacks.
  static int compressBrotli(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len);
  static int decompressBrotli(SSL* ssl, CRYPTO_BUFFER** out, size_t uncompressed_len,
                              const uint8_t* in, size_t in_len);

  // Zstd compression callbacks.
  static int compressZstd(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len);
  static int decompressZstd(SSL* ssl, CRYPTO_BUFFER** out, size_t uncompressed_len,
                            const uint8_t* in, size_t in_len);

  // Zlib compression callbacks.
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
