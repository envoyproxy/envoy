#include "source/common/tls/cert_compression.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/tls/stats.h"

#include "brotli/decode.h"
#include "brotli/encode.h"
#include "openssl/tls1.h"
#include "zstd.h"

#define ZLIB_CONST
#include "zlib.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

namespace {

class ScopedZStream {
public:
  using CleanupFunc = int (*)(z_stream*);

  ScopedZStream(z_stream& z, CleanupFunc cleanup) : z_(z), cleanup_(cleanup) {}
  ~ScopedZStream() { cleanup_(&z_); }

private:
  z_stream& z_;
  CleanupFunc cleanup_;
};

// ex_data index for Stats::Scope* - avoids conflict with QUICHE own SSL_CTX usage.
int getScopeExDataIndex() {
  static int index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  return index;
}

void recordCompressedCertSize(SSL* ssl, size_t uncompressed_size, size_t compressed_size,
                              const std::string& algo) {
  if (ssl == nullptr) {
    return;
  }
  SSL_CTX* ssl_ctx = SSL_get_SSL_CTX(ssl);
  if (ssl_ctx == nullptr) {
    return;
  }
  auto* scope = static_cast<Stats::Scope*>(SSL_CTX_get_ex_data(ssl_ctx, getScopeExDataIndex()));
  if (scope != nullptr) {
    const std::string prefix = "ssl.certificate_compression." + algo + ".";
    CertCompressionStats stats = generateCertCompressionStats(*scope, prefix);
    stats.compressed_.inc();
    stats.total_uncompressed_bytes_.add(uncompressed_size);
    stats.total_compressed_bytes_.add(compressed_size);
  }
}

} // namespace

void CertCompression::registerBrotli(SSL_CTX* ssl_ctx) {
  auto ret = SSL_CTX_add_cert_compression_alg(ssl_ctx, TLSEXT_cert_compression_brotli,
                                              compressBrotli, decompressBrotli);
  ASSERT(ret == 1);
}

void CertCompression::registerZstd(SSL_CTX* ssl_ctx) {
  auto ret = SSL_CTX_add_cert_compression_alg(ssl_ctx, TLSEXT_cert_compression_zstd, compressZstd,
                                              decompressZstd);
  ASSERT(ret == 1);
}

void CertCompression::registerZlib(SSL_CTX* ssl_ctx) {
  auto ret = SSL_CTX_add_cert_compression_alg(ssl_ctx, TLSEXT_cert_compression_zlib, compressZlib,
                                              decompressZlib);
  ASSERT(ret == 1);
}

void CertCompression::registerAlgorithms(SSL_CTX* ssl_ctx, const std::vector<Algorithm>& algorithms,
                                         Stats::Scope* scope) {
  if (scope != nullptr) {
    SSL_CTX_set_ex_data(ssl_ctx, getScopeExDataIndex(), scope);
  }
  for (const auto& algo : algorithms) {
    switch (algo) {
    case Algorithm::Brotli:
      registerBrotli(ssl_ctx);
      break;
    case Algorithm::Zstd:
      registerZstd(ssl_ctx);
      break;
    case Algorithm::Zlib:
      registerZlib(ssl_ctx);
      break;
    }
  }
}

int CertCompression::compressBrotli(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len) {
  size_t encoded_size = BrotliEncoderMaxCompressedSize(in_len);
  if (encoded_size == 0) {
    IS_ENVOY_BUG("BrotliEncoderMaxCompressedSize returned 0");
    return FAILURE;
  }

  uint8_t* out_buf = nullptr;
  if (!CBB_reserve(out, &out_buf, encoded_size)) {
    IS_ENVOY_BUG(fmt::format("Cert compression failure in allocating output CBB buffer of size {}",
                             encoded_size));
    return FAILURE;
  }

  if (BrotliEncoderCompress(BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_GENERIC,
                            in_len, in, &encoded_size, out_buf) != BROTLI_TRUE) {
    IS_ENVOY_BUG("Cert compression failure in BrotliEncoderCompress");
    return FAILURE;
  }

  if (!CBB_did_write(out, encoded_size)) {
    IS_ENVOY_BUG("CBB_did_write failed");
    return FAILURE;
  }

  recordCompressedCertSize(ssl, in_len, encoded_size, "brotli");
  ENVOY_LOG(trace, "Cert brotli compression successful");
  return SUCCESS;
}

int CertCompression::decompressBrotli(SSL*, CRYPTO_BUFFER** out, size_t uncompressed_len,
                                      const uint8_t* in, size_t in_len) {
  uint8_t* out_buf = nullptr;
  bssl::UniquePtr<CRYPTO_BUFFER> decompressed_data(CRYPTO_BUFFER_alloc(&out_buf, uncompressed_len));
  if (!decompressed_data) {
    IS_ENVOY_BUG("Failed to allocate CRYPTO_BUFFER for brotli decompression");
    return FAILURE;
  }

  size_t decoded_size = uncompressed_len;
  BrotliDecoderResult result = BrotliDecoderDecompress(in_len, in, &decoded_size, out_buf);

  if (result != BROTLI_DECODER_RESULT_SUCCESS) {
    ENVOY_LOG_PERIODIC(error, std::chrono::seconds(10),
                       "Cert brotli decompression failure, possibly caused by invalid "
                       "compressed cert from peer: result={}, decoded_size={}, uncompressed_len={}",
                       static_cast<int>(result), decoded_size, uncompressed_len);
    return FAILURE;
  }

  if (decoded_size != uncompressed_len) {
    ENVOY_LOG_PERIODIC(error, std::chrono::seconds(10),
                       "Brotli decompression length did not match peer provided uncompressed "
                       "length, caused by either invalid peer handshake data or decompression "
                       "error: decoded_size={}, uncompressed_len={}",
                       decoded_size, uncompressed_len);
    return FAILURE;
  }

  ENVOY_LOG(trace, "Cert brotli decompression successful");
  *out = decompressed_data.release();
  return SUCCESS;
}

int CertCompression::compressZstd(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len) {
  size_t const max_size = ZSTD_compressBound(in_len);
  if (max_size == 0) {
    IS_ENVOY_BUG("ZSTD_compressBound returned 0");
    return FAILURE;
  }

  uint8_t* out_buf = nullptr;
  if (!CBB_reserve(out, &out_buf, max_size)) {
    IS_ENVOY_BUG(fmt::format("Cert compression failure in allocating output CBB buffer of size {}",
                             max_size));
    return FAILURE;
  }

  size_t const compressed_size = ZSTD_compress(out_buf, max_size, in, in_len, ZSTD_CLEVEL_DEFAULT);
  if (ZSTD_isError(compressed_size)) {
    IS_ENVOY_BUG(
        fmt::format("Cert zstd compression failure: {}", ZSTD_getErrorName(compressed_size)));
    return FAILURE;
  }

  if (!CBB_did_write(out, compressed_size)) {
    IS_ENVOY_BUG("CBB_did_write failed");
    return FAILURE;
  }

  recordCompressedCertSize(ssl, in_len, compressed_size, "zstd");
  ENVOY_LOG(trace, "Cert zstd compression successful");
  return SUCCESS;
}

int CertCompression::decompressZstd(SSL*, CRYPTO_BUFFER** out, size_t uncompressed_len,
                                    const uint8_t* in, size_t in_len) {
  uint8_t* out_buf = nullptr;
  bssl::UniquePtr<CRYPTO_BUFFER> decompressed_data(CRYPTO_BUFFER_alloc(&out_buf, uncompressed_len));
  if (!decompressed_data) {
    IS_ENVOY_BUG("Failed to allocate CRYPTO_BUFFER for zstd decompression");
    return FAILURE;
  }

  size_t const decompressed_size = ZSTD_decompress(out_buf, uncompressed_len, in, in_len);
  if (ZSTD_isError(decompressed_size)) {
    ENVOY_LOG_PERIODIC(error, std::chrono::seconds(10),
                       "Cert zstd decompression failure, possibly caused by invalid "
                       "compressed cert from peer: {}, uncompressed_len={}",
                       ZSTD_getErrorName(decompressed_size), uncompressed_len);
    return FAILURE;
  }

  if (decompressed_size != uncompressed_len) {
    ENVOY_LOG_PERIODIC(error, std::chrono::seconds(10),
                       "Zstd decompression length did not match peer provided uncompressed "
                       "length, caused by either invalid peer handshake data or decompression "
                       "error: decompressed_size={}, uncompressed_len={}",
                       decompressed_size, uncompressed_len);
    return FAILURE;
  }

  ENVOY_LOG(trace, "Cert zstd decompression successful");
  *out = decompressed_data.release();
  return SUCCESS;
}

int CertCompression::compressZlib(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len) {
  z_stream z = {};
  // The deflateInit macro from zlib.h contains an old-style cast, so we need to suppress the
  // warning for this call.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
  int rv = deflateInit(&z, Z_DEFAULT_COMPRESSION);
#pragma GCC diagnostic pop
  if (rv != Z_OK) {
    IS_ENVOY_BUG(fmt::format("Cert compression failure in deflateInit: {}", rv));
    return FAILURE;
  }

  ScopedZStream deleter(z, deflateEnd);

  const auto upper_bound = deflateBound(&z, in_len);

  uint8_t* out_buf = nullptr;
  if (!CBB_reserve(out, &out_buf, upper_bound)) {
    IS_ENVOY_BUG(fmt::format("Cert compression failure in allocating output CBB buffer of size {}",
                             upper_bound));
    return FAILURE;
  }

  z.next_in = in;
  z.avail_in = in_len;
  z.next_out = out_buf;
  z.avail_out = upper_bound;

  rv = deflate(&z, Z_FINISH);
  if (rv != Z_STREAM_END) {
    IS_ENVOY_BUG(fmt::format(
        "Cert compression failure in deflate: {}, z.total_out {}, in_len {}, z.avail_in {}", rv,
        z.avail_in, in_len, z.avail_in));
    return FAILURE;
  }

  if (!CBB_did_write(out, z.total_out)) {
    IS_ENVOY_BUG("CBB_did_write failed");
    return FAILURE;
  }

  recordCompressedCertSize(ssl, in_len, z.total_out, "zlib");
  ENVOY_LOG(trace, "Cert zlib compression successful");
  return SUCCESS;
}

int CertCompression::decompressZlib(SSL*, CRYPTO_BUFFER** out, size_t uncompressed_len,
                                    const uint8_t* in, size_t in_len) {
  z_stream z = {};
  // The inflateInit macro from zlib.h contains an old-style cast, so we need to suppress the
  // warning for this call.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
  int rv = inflateInit(&z);
#pragma GCC diagnostic pop
  if (rv != Z_OK) {
    IS_ENVOY_BUG(fmt::format("Cert decompression failure in inflateInit: {}", rv));
    return FAILURE;
  }

  ScopedZStream deleter(z, inflateEnd);

  z.next_in = in;
  z.avail_in = in_len;
  bssl::UniquePtr<CRYPTO_BUFFER> decompressed_data(
      CRYPTO_BUFFER_alloc(&z.next_out, uncompressed_len));
  if (!decompressed_data) {
    IS_ENVOY_BUG("Failed to allocate CRYPTO_BUFFER for zlib decompression");
    return FAILURE;
  }
  z.avail_out = uncompressed_len;

  rv = inflate(&z, Z_FINISH);
  if (rv != Z_STREAM_END) {
    ENVOY_LOG_PERIODIC(error, std::chrono::seconds(10),
                       "Cert zlib decompression failure, possibly caused by invalid "
                       "compressed cert from peer: {}, z.total_out {}, uncompressed_len {}",
                       rv, z.total_out, uncompressed_len);
    return FAILURE;
  }

  if (z.total_out != uncompressed_len) {
    ENVOY_LOG_PERIODIC(error, std::chrono::seconds(10),
                       "Zlib decompression length did not match peer provided uncompressed "
                       "length, caused by either invalid peer handshake data or decompression "
                       "error: z.total_out={}, uncompressed_len={}",
                       z.total_out, uncompressed_len);
    return FAILURE;
  }

  ENVOY_LOG(trace, "Cert zlib decompression successful");
  *out = decompressed_data.release();
  return SUCCESS;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
