#include "source/common/tls/cert_decompression.h"

#include "source/common/common/assert.h"

#include "brotli/decode.h"
#include "openssl/tls1.h"

#define ZLIB_CONST
#include "zlib.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

#ifdef ENVOY_SSL_OPENSSL
void CertDecompression::registerBrotli(SSL_CTX*) {}
void CertDecompression::registerZlib(SSL_CTX*) {}
#else // ENVOY_SSL_OPENSSL

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

} // namespace

void CertDecompression::registerBrotli(SSL_CTX* ssl_ctx) {
  auto ret = SSL_CTX_add_cert_compression_alg(ssl_ctx, TLSEXT_cert_compression_brotli,
                                              /*compress=*/nullptr, decompressBrotli);
  ASSERT(ret == 1);
}

void CertDecompression::registerZlib(SSL_CTX* ssl_ctx) {
  auto ret = SSL_CTX_add_cert_compression_alg(ssl_ctx, TLSEXT_cert_compression_zlib,
                                              /*compress=*/nullptr, decompressZlib);
  ASSERT(ret == 1);
}

int CertDecompression::decompressBrotli(SSL*, CRYPTO_BUFFER** out, size_t uncompressed_len,
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

int CertDecompression::decompressZlib(SSL*, CRYPTO_BUFFER** out, size_t uncompressed_len,
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
