#include "test/test_common/tls_utility.h"

#include "common/common/assert.h"
#include "common/common/hex.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Tls {
namespace Test {

std::vector<uint8_t> generateClientHello(const ClientHelloOptions& options) {
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_with_buffers_method()));

  long flags = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
  if (options.no_tls_v1_2_) {
    flags |= SSL_OP_NO_TLSv1_2;
  }
  SSL_CTX_set_options(ctx.get(), flags);

  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));

  // Ownership of these is passed to *ssl
  BIO* in = BIO_new(BIO_s_mem());
  BIO* out = BIO_new(BIO_s_mem());
  SSL_set_bio(ssl.get(), in, out);

  SSL_set_connect_state(ssl.get());
  const char* const PREFERRED_CIPHERS = "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4";
  RELEASE_ASSERT(SSL_set_cipher_list(ssl.get(), PREFERRED_CIPHERS) != 0, "");
  if (!options.sni_name_.empty()) {
    RELEASE_ASSERT(SSL_set_tlsext_host_name(ssl.get(), options.sni_name_.c_str()) != 0, "");
  }
  if (!options.alpn_.empty()) {
    RELEASE_ASSERT(SSL_set_alpn_protos(ssl.get(),
                                       reinterpret_cast<const uint8_t*>(options.alpn_.data()),
                                       options.alpn_.size()) == 0,
                   "");
  }
  if (!options.cipher_suites_.empty()) {
    RELEASE_ASSERT(SSL_set_strict_cipher_list(ssl.get(), options.cipher_suites_.c_str()) != 0, "");
  }
  if (!options.ecdh_curves_.empty()) {
    RELEASE_ASSERT(SSL_set1_curves_list(ssl.get(), options.ecdh_curves_.c_str()) != 0, "");
  }
  RELEASE_ASSERT(SSL_do_handshake(ssl.get()) != 0, "");
  const uint8_t* data = NULL;
  size_t data_len = 0;
  BIO_mem_contents(out, &data, &data_len);
  ASSERT(data_len > 0);
  std::vector<uint8_t> buf(data, data + data_len);
  ENVOY_LOG_MISC(debug, "ClientHello: {}", Hex::encode(buf));
  for (uint32_t i = 0; i < buf.size(); ++i) {
    ENVOY_LOG_MISC(trace, "ClientHello: [{}] 0x{}", i, Hex::encode({buf[i]}));
  }
  return buf;
}

} // namespace Test
} // namespace Tls
} // namespace Envoy
