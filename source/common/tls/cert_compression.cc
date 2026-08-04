#include "source/common/tls/cert_compression.h"

#include <memory>
#include <string>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "brotli/decode.h"
#include "brotli/encode.h"
#include "openssl/tls1.h"

#define ZLIB_CONST
#include "zlib.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

#ifdef ENVOY_SSL_OPENSSL
void CertCompression::registerBrotli(SSL_CTX*) {}
void CertCompression::registerZlib(SSL_CTX*) {}
#else // ENVOY_SSL_OPENSSL

namespace {

struct CompressedCertCache {
  absl::Mutex mu;
  absl::flat_hash_map<std::pair<uint16_t, std::string>, std::string>
      compressed_by_alg_and_chain ABSL_GUARDED_BY(mu);
};

void freeCompressedCertCache(void*, void* ptr, CRYPTO_EX_DATA*, int, long, void*) {
  delete static_cast<CompressedCertCache*>(ptr);
}

int sslCtxCacheIndex() {
  CONSTRUCT_ON_FIRST_USE(int, []() -> int {
    const int index =
        SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, freeCompressedCertCache);
    RELEASE_ASSERT(index >= 0, "");
    return index;
  }());
}

void ensureCompressedCertCache(SSL_CTX* ssl_ctx) {
  if (SSL_CTX_get_ex_data(ssl_ctx, sslCtxCacheIndex()) != nullptr) {
    return;
  }
  auto cache = std::make_unique<CompressedCertCache>();
  if (SSL_CTX_set_ex_data(ssl_ctx, sslCtxCacheIndex(), cache.get()) == 1) {
    cache.release();
  }
}

int writeToCbb(CBB* out, const std::string& data) {
  if (CBB_add_bytes(out, reinterpret_cast<const uint8_t*>(data.data()), data.size()) != 1) {
    IS_ENVOY_BUG("Cert compression failure writing compressed certificate to output buffer");
    return CertCompression::FAILURE;
  }
  return CertCompression::SUCCESS;
}

int compressCached(SSL* ssl, uint16_t alg,
                   std::optional<std::string> (*compressor)(const uint8_t*, size_t), CBB* out,
                   const uint8_t* in, size_t in_len) {
  ASSERT(ssl != nullptr);
  auto* cache = static_cast<CompressedCertCache*>(
      SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), sslCtxCacheIndex()));
  ASSERT(cache != nullptr);

  std::pair<uint16_t, std::string> key(alg, std::string(reinterpret_cast<const char*>(in), in_len));

  {
    absl::ReaderMutexLock lock(&cache->mu);
    auto it = cache->compressed_by_alg_and_chain.find(key);
    if (it != cache->compressed_by_alg_and_chain.end()) {
      return writeToCbb(out, it->second);
    }
  }

  std::optional<std::string> compressed = compressor(in, in_len);
  if (!compressed.has_value()) {
    return CertCompression::FAILURE;
  }
  absl::WriterMutexLock lock(&cache->mu);
  auto it =
      cache->compressed_by_alg_and_chain.try_emplace(std::move(key), std::move(*compressed)).first;
  return writeToCbb(out, it->second);
}

std::optional<std::string> doBrotliCompress(const uint8_t* in, size_t in_len) {
  size_t encoded_size = BrotliEncoderMaxCompressedSize(in_len);
  if (encoded_size == 0) {
    IS_ENVOY_BUG("BrotliEncoderMaxCompressedSize returned 0");
    return std::nullopt;
  }

  std::string compressed(encoded_size, '\0');
  if (BrotliEncoderCompress(BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_GENERIC,
                            in_len, in, &encoded_size,
                            reinterpret_cast<uint8_t*>(compressed.data())) != BROTLI_TRUE) {
    IS_ENVOY_BUG("Cert compression failure in BrotliEncoderCompress");
    return std::nullopt;
  }

  compressed.resize(encoded_size);
  return compressed;
}

} // namespace

void CertCompression::registerBrotli(SSL_CTX* ssl_ctx) {
  auto ret = SSL_CTX_add_cert_compression_alg(ssl_ctx, TLSEXT_cert_compression_brotli,
                                              compressBrotli, decompressBrotli);
  ASSERT(ret == 1);
  ensureCompressedCertCache(ssl_ctx);
}

void CertCompression::registerZlib(SSL_CTX* ssl_ctx) {
  auto ret = SSL_CTX_add_cert_compression_alg(ssl_ctx, TLSEXT_cert_compression_zlib, compressZlib,
                                              decompressZlib);
  ASSERT(ret == 1);
  ensureCompressedCertCache(ssl_ctx);
}

int CertCompression::compressBrotli(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len) {
  const int rc =
      compressCached(ssl, TLSEXT_cert_compression_brotli, doBrotliCompress, out, in, in_len);
  if (rc == SUCCESS) {
    ENVOY_LOG(trace, "Cert brotli compression successful");
  }
  return rc;
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

std::optional<std::string> doZlibCompress(const uint8_t* in, size_t in_len) {
  z_stream z = {};
  // The deflateInit macro from zlib.h contains an old-style cast, so we need to suppress the
  // warning for this call.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
  int rv = deflateInit(&z, Z_DEFAULT_COMPRESSION);
#pragma GCC diagnostic pop
  if (rv != Z_OK) {
    IS_ENVOY_BUG(fmt::format("Cert compression failure in deflateInit: {}", rv));
    return std::nullopt;
  }

  ScopedZStream deleter(z, deflateEnd);

  const auto upper_bound = deflateBound(&z, in_len);
  std::string compressed(upper_bound, '\0');

  z.next_in = in;
  z.avail_in = in_len;
  z.next_out = reinterpret_cast<uint8_t*>(compressed.data());
  z.avail_out = upper_bound;

  rv = deflate(&z, Z_FINISH);
  if (rv != Z_STREAM_END) {
    IS_ENVOY_BUG(fmt::format(
        "Cert compression failure in deflate: {}, z.total_out {}, in_len {}, z.avail_in {}", rv,
        z.avail_in, in_len, z.avail_in));
    return std::nullopt;
  }

  compressed.resize(z.total_out);
  return compressed;
}

} // namespace

int CertCompression::compressZlib(SSL* ssl, CBB* out, const uint8_t* in, size_t in_len) {
  const int rc = compressCached(ssl, TLSEXT_cert_compression_zlib, doZlibCompress, out, in, in_len);
  if (rc == SUCCESS) {
    ENVOY_LOG(trace, "Cert zlib compression successful");
  }
  return rc;
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
  uint8_t* out_buf = nullptr;
  bssl::UniquePtr<CRYPTO_BUFFER> decompressed_data(CRYPTO_BUFFER_alloc(&out_buf, uncompressed_len));
  if (!decompressed_data) {
    IS_ENVOY_BUG("Failed to allocate CRYPTO_BUFFER for zlib decompression");
    return FAILURE;
  }
  z.next_out = out_buf;
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
#endif // ENVOY_SSL_OPENSSL

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
