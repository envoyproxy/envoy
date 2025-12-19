#pragma once

#include <vector>

#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// RFC 8879 Certificate Compression Algorithm IDs.
// TLSEXT_cert_compression_zlib (1) and TLSEXT_cert_compression_brotli (2) are defined in BoringSSL.
// Define zstd (3) locally as BoringSSL does not define it.
#ifndef TLSEXT_cert_compression_zstd
#define TLSEXT_cert_compression_zstd 3
#endif

// RFC 8879 TLS Certificate Compression.
class CertCompression : protected Logger::Loggable<Logger::Id::connection> {
public:
  enum class Algorithm : uint16_t {
    Zlib = 1,
    Brotli = 2,
    Zstd = 3,
  };

  // Registers algorithms in the order provided (first = highest priority).
  static void registerAlgorithms(SSL_CTX* ssl_ctx, const std::vector<Algorithm>& algorithms,
                                 Stats::Scope* scope = nullptr);

  static void registerBrotli(SSL_CTX* ssl_ctx);
  static void registerZstd(SSL_CTX* ssl_ctx);
  static void registerZlib(SSL_CTX* ssl_ctx);

  static int compressBrotli(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len);
  static int decompressBrotli(SSL* ssl, CRYPTO_BUFFER** out, size_t uncompressed_len,
                              const uint8_t* in, size_t in_len);

  static int compressZstd(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len);
  static int decompressZstd(SSL* ssl, CRYPTO_BUFFER** out, size_t uncompressed_len,
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
