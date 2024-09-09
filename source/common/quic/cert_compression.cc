#include "source/common/quic/cert_compression.h"

#include "source/common/common/assert.h"
#include "source/common/runtime/runtime_features.h"

#include "openssl/tls1.h"

#define ZLIB_CONST
#include "zlib.h"

namespace Envoy {
namespace Quic {

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

void CertCompression::registerSslContext(SSL_CTX* ssl_ctx) {
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.quic_support_certificate_compression")) {
    auto ret = SSL_CTX_add_cert_compression_alg(ssl_ctx, TLSEXT_cert_compression_zlib, compressZlib,
                                                decompressZlib);
    ASSERT(ret == 1);
  }
}

int CertCompression::compressZlib(SSL*, CBB* out, const uint8_t* in, size_t in_len) {

  z_stream z = {};
  int rv = deflateInit(&z, Z_DEFAULT_COMPRESSION);
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

  ENVOY_LOG(trace, "Cert compression successful");

  return SUCCESS;
}

int CertCompression::decompressZlib(SSL*, CRYPTO_BUFFER** out, size_t uncompressed_len,
                                    const uint8_t* in, size_t in_len) {
  z_stream z = {};
  int rv = inflateInit(&z);
  if (rv != Z_OK) {
    IS_ENVOY_BUG(fmt::format("Cert decompression failure in inflateInit: {}", rv));
    return FAILURE;
  }

  ScopedZStream deleter(z, inflateEnd);

  z.next_in = in;
  z.avail_in = in_len;
  bssl::UniquePtr<CRYPTO_BUFFER> decompressed_data(
      CRYPTO_BUFFER_alloc(&z.next_out, uncompressed_len));
  z.avail_out = uncompressed_len;

  rv = inflate(&z, Z_FINISH);
  if (rv != Z_STREAM_END) {
    ENVOY_LOG_PERIODIC(error, std::chrono::seconds(10),
                       "Cert decompression failure in inflate, possibly caused by invalid "
                       "compressed cert from peer: {}, z.total_out {}, uncompressed_len {}",
                       rv, z.total_out, uncompressed_len);
    return FAILURE;
  }

  if (z.total_out != uncompressed_len) {
    ENVOY_LOG_PERIODIC(error, std::chrono::seconds(10),
                       "Decompression length did not match peer provided uncompressed length, "
                       "caused by either invalid peer handshake data or decompression error.");
    return FAILURE;
  }

  ENVOY_LOG(trace, "Cert decompression successful");

  *out = decompressed_data.release();
  return SUCCESS;
}

} // namespace Quic
} // namespace Envoy
