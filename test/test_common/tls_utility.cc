#include "test/test_common/tls_utility.h"

#include "common/common/assert.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Tls {
namespace Test {

std::vector<uint8_t> generateClientHello(const std::string& sni_name, const std::string& alpn) {
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_with_buffers_method()));

  const long flags = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
  SSL_CTX_set_options(ctx.get(), flags);

  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));

  // Ownership of these is passed to *ssl
  BIO* in = BIO_new(BIO_s_mem());
  BIO* out = BIO_new(BIO_s_mem());
  SSL_set_bio(ssl.get(), in, out);

  SSL_set_connect_state(ssl.get());
  const char* const PREFERRED_CIPHERS = "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4";
  SSL_set_cipher_list(ssl.get(), PREFERRED_CIPHERS);
  if (!sni_name.empty()) {
    SSL_set_tlsext_host_name(ssl.get(), sni_name.c_str());
  }
  if (!alpn.empty()) {
    SSL_set_alpn_protos(ssl.get(), reinterpret_cast<const uint8_t*>(alpn.data()), alpn.size());
  }
  SSL_do_handshake(ssl.get());
  const uint8_t* data = NULL;
  size_t data_len = 0;
  BIO_mem_contents(out, &data, &data_len);
  ASSERT(data_len > 0);
  std::vector<uint8_t> buf(data, data + data_len);
  return buf;
}

bssl::UniquePtr<X509> readCertFromFile(const std::string& path) {
  const std::string& file_content = TestEnvironment::readFileToStringForTest(path);
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(file_content.c_str(), file_content.size()));
  bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  EXPECT_NE(cert, nullptr);
  return cert;
}

} // namespace Test
} // namespace Tls
} // namespace Envoy
