#include "source/common/tls/cert_compression.h"

#include <memory>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/common/thread.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"
#include "brotli/decode.h"
#include "brotli/encode.h"
#include "openssl/sha.h"
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

// A compressed certificate chain together with a hash of the uncompressed chain
// it was produced from. The hash lets us assert (in debug builds) that a cached
// entry is only ever reused for the chain it was computed from.
struct CachedCompressedCert {
  std::string compressed;
  std::string chain_hash;
};

// Per-certificate cache of compressed certificates, attached to the SSL_CTX so
// it lives as long as the certificate/context. Keyed by TLS cert-compression
// algorithm id: the client selects the algorithm (brotli or zlib), and Envoy
// uses one certificate chain per SSL_CTX, so a slot per algorithm is enough.
// This mirrors OpenSSL's per-algorithm array on the credential
// (CERT_PKEY.comp_cert[]).
struct CompressedCertCache {
  absl::Mutex mu;
  absl::flat_hash_map<uint16_t, CachedCompressedCert> by_alg ABSL_GUARDED_BY(mu);
};

std::string certChainHash(const uint8_t* in, size_t in_len) {
  std::string hash(SHA256_DIGEST_LENGTH, '\0');
  SHA256(in, in_len, reinterpret_cast<uint8_t*>(hash.data()));
  return hash;
}

void freeCompressedCertCache(void* /*parent*/, void* ptr, CRYPTO_EX_DATA* /*ad*/, int /*index*/,
                             long /*argl*/, void* /*argp*/) {
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

// Attaches an empty cache to the context if it doesn't have one yet. Called from
// the registration path, which runs single-threaded during configuration before
// any handshake.
void ensureCompressedCertCache(SSL_CTX* ssl_ctx) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (SSL_CTX_get_ex_data(ssl_ctx, sslCtxCacheIndex()) != nullptr) {
    return;
  }
  auto cache = std::make_unique<CompressedCertCache>();
  if (SSL_CTX_set_ex_data(ssl_ctx, sslCtxCacheIndex(), cache.get()) == 1) {
    // Ownership transferred to the SSL_CTX; freed by freeCompressedCertCache.
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

// Compresses the certificate payload for `alg` and writes it to `out`, reusing
// the per-context cached copy so the compression runs once per certificate
// rather than once per handshake. BoringSSL only invokes the compression
// callbacks during a handshake, with a valid SSL whose SSL_CTX had a cache
// attached at registration, so both are required here.
int compressCached(SSL* ssl, uint16_t alg,
                   absl::optional<std::string> (*compressor)(const uint8_t*, size_t), CBB* out,
                   const uint8_t* in, size_t in_len) {
  ASSERT(ssl != nullptr);
  auto* cache = static_cast<CompressedCertCache*>(
      SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), sslCtxCacheIndex()));
  ASSERT(cache != nullptr);

  // Fast path: handshakes that hit the cache share a reader lock, so concurrent
  // workers don't serialize on the steady-state lookup.
  {
    absl::ReaderMutexLock lock(&cache->mu);
    auto it = cache->by_alg.find(alg);
    if (it != cache->by_alg.end()) {
      // There is one certificate chain per SSL_CTX, so a cached entry must
      // correspond to the chain we were handed; verify in debug builds.
      ASSERT(certChainHash(in, in_len) == it->second.chain_hash,
             "compressed certificate cache hit for a different certificate chain");
      return writeToCbb(out, it->second.compressed);
    }
  }

  // Slow path: compress without holding the lock, then store under the writer
  // lock. try_emplace keeps a racing worker's entry if it got there first.
  absl::optional<std::string> compressed = compressor(in, in_len);
  if (!compressed.has_value()) {
    return CertCompression::FAILURE;
  }
  absl::WriterMutexLock lock(&cache->mu);
  auto it =
      cache->by_alg
          .try_emplace(alg, CachedCompressedCert{std::move(*compressed), certChainHash(in, in_len)})
          .first;
  return writeToCbb(out, it->second.compressed);
}

absl::optional<std::string> doBrotliCompress(const uint8_t* in, size_t in_len) {
  size_t encoded_size = BrotliEncoderMaxCompressedSize(in_len);
  if (encoded_size == 0) {
    IS_ENVOY_BUG("BrotliEncoderMaxCompressedSize returned 0");
    return absl::nullopt;
  }

  std::string compressed(encoded_size, '\0');
  if (BrotliEncoderCompress(BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_GENERIC,
                            in_len, in, &encoded_size,
                            reinterpret_cast<uint8_t*>(compressed.data())) != BROTLI_TRUE) {
    IS_ENVOY_BUG("Cert compression failure in BrotliEncoderCompress");
    return absl::nullopt;
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

absl::optional<std::string> doZlibCompress(const uint8_t* in, size_t in_len) {
  z_stream z = {};
  // The deflateInit macro from zlib.h contains an old-style cast, so we need to suppress the
  // warning for this call.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
  int rv = deflateInit(&z, Z_DEFAULT_COMPRESSION);
#pragma GCC diagnostic pop
  if (rv != Z_OK) {
    IS_ENVOY_BUG(fmt::format("Cert compression failure in deflateInit: {}", rv));
    return absl::nullopt;
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
    return absl::nullopt;
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
