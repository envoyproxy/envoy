#include <string>
#include <vector>

#include "source/common/tls/cert_compression.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "openssl/bn.h"
#include "openssl/ec_key.h"
#include "openssl/evp.h"
#include "openssl/nid.h"
#include "openssl/rsa.h"
#include "openssl/x509.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

#ifndef ENVOY_SSL_OPENSSL
// Test data for round-trip compression tests
constexpr uint8_t kTestData[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
constexpr size_t kTestDataLen = sizeof(kTestData);

// Builds an SSL whose context has both compression algorithms (and therefore their
// per-context compressed-cert caches) registered, mirroring the production handshake
// path. The compression callbacks require a valid SSL with a cache attached, so tests
// drive them through this rather than a null SSL. The object keeps the SSL_CTX alive
// for the lifetime of the SSL.
class RegisteredSsl {
public:
  RegisteredSsl() {
    ctx_.reset(SSL_CTX_new(TLS_method()));
    CertCompression::registerBrotli(ctx_.get());
    CertCompression::registerZlib(ctx_.get());
    ssl_.reset(SSL_new(ctx_.get()));
  }
  SSL* get() const { return ssl_.get(); }

private:
  bssl::UniquePtr<SSL_CTX> ctx_;
  bssl::UniquePtr<SSL> ssl_;
};

//
// Brotli Tests
//

TEST(CertCompressionBrotliTest, RoundTrip) {
  RegisteredSsl reg;
  // Compress
  bssl::ScopedCBB compressed;
  ASSERT_EQ(1, CBB_init(compressed.get(), 0));
  EXPECT_EQ(CertCompression::SUCCESS,
            CertCompression::compressBrotli(reg.get(), compressed.get(), kTestData, kTestDataLen));
  const auto compressed_len = CBB_len(compressed.get());
  EXPECT_GT(compressed_len, 0u);

  // Decompress
  CRYPTO_BUFFER* out = nullptr;
  EXPECT_EQ(CertCompression::SUCCESS,
            CertCompression::decompressBrotli(nullptr, &out, kTestDataLen,
                                              CBB_data(compressed.get()), compressed_len));
  ASSERT_NE(nullptr, out);
  bssl::UniquePtr<CRYPTO_BUFFER> out_ptr(out);

  // Verify
  EXPECT_EQ(absl::Span<const uint8_t>(kTestData, kTestDataLen),
            absl::Span<const uint8_t>(CRYPTO_BUFFER_data(out), CRYPTO_BUFFER_len(out)));
}

// A cache hit must return the same bytes as a freshly computed compression, and
// repeated calls must be stable -- the first call is a cache miss that computes
// and stores, the second is served from the per-context cache.
TEST(CertCompressionBrotliTest, CachedMatchesUncached) {
  // Reference from a separate context, whose first compression is a cache miss
  // and therefore freshly computed.
  RegisteredSsl ref;
  bssl::ScopedCBB ref_cbb;
  ASSERT_EQ(1, CBB_init(ref_cbb.get(), 0));
  ASSERT_EQ(CertCompression::SUCCESS,
            CertCompression::compressBrotli(ref.get(), ref_cbb.get(), kTestData, kTestDataLen));
  const std::string reference(reinterpret_cast<const char*>(CBB_data(ref_cbb.get())),
                              CBB_len(ref_cbb.get()));

  RegisteredSsl reg;
  auto compress = [&]() {
    bssl::ScopedCBB cbb;
    EXPECT_EQ(1, CBB_init(cbb.get(), 0));
    EXPECT_EQ(CertCompression::SUCCESS,
              CertCompression::compressBrotli(reg.get(), cbb.get(), kTestData, kTestDataLen));
    return std::string(reinterpret_cast<const char*>(CBB_data(cbb.get())), CBB_len(cbb.get()));
  };

  EXPECT_EQ(reference, compress()); // miss: computes and caches
  EXPECT_EQ(reference, compress()); // hit: served from cache
}

TEST(CertCompressionBrotliTest, DecompressBadData) {
  EXPECT_LOG_CONTAINS(
      "error",
      "Cert brotli decompression failure, possibly caused by invalid compressed cert from peer", {
        CRYPTO_BUFFER* out = nullptr;
        const uint8_t bad_compressed_data = 1;
        EXPECT_EQ(CertCompression::FAILURE,
                  CertCompression::decompressBrotli(nullptr, &out, 100, &bad_compressed_data,
                                                    sizeof(bad_compressed_data)));
      });
}

TEST(CertCompressionBrotliTest, DecompressBadLength) {
  RegisteredSsl reg;
  bssl::ScopedCBB compressed;
  ASSERT_EQ(1, CBB_init(compressed.get(), 0));
  ASSERT_EQ(CertCompression::SUCCESS,
            CertCompression::compressBrotli(reg.get(), compressed.get(), kTestData, kTestDataLen));
  const auto compressed_len = CBB_len(compressed.get());
  EXPECT_GT(compressed_len, 0u);

  EXPECT_LOG_CONTAINS(
      "error", "Brotli decompression length did not match peer provided uncompressed length", {
        CRYPTO_BUFFER* out = nullptr;
        EXPECT_EQ(CertCompression::FAILURE,
                  CertCompression::decompressBrotli(nullptr, &out,
                                                    kTestDataLen + 1 /* intentionally incorrect */,
                                                    CBB_data(compressed.get()), compressed_len));
      });
}

//
// Zlib Tests
//

TEST(CertCompressionZlibTest, RoundTrip) {
  RegisteredSsl reg;
  // Compress
  bssl::ScopedCBB compressed;
  ASSERT_EQ(1, CBB_init(compressed.get(), 0));
  EXPECT_EQ(CertCompression::SUCCESS,
            CertCompression::compressZlib(reg.get(), compressed.get(), kTestData, kTestDataLen));
  const auto compressed_len = CBB_len(compressed.get());
  EXPECT_GT(compressed_len, 0u);

  // Decompress
  CRYPTO_BUFFER* out = nullptr;
  EXPECT_EQ(CertCompression::SUCCESS,
            CertCompression::decompressZlib(nullptr, &out, kTestDataLen, CBB_data(compressed.get()),
                                            compressed_len));
  ASSERT_NE(nullptr, out);
  bssl::UniquePtr<CRYPTO_BUFFER> out_ptr(out);

  // Verify
  EXPECT_EQ(absl::Span<const uint8_t>(kTestData, kTestDataLen),
            absl::Span<const uint8_t>(CRYPTO_BUFFER_data(out), CRYPTO_BUFFER_len(out)));
}

TEST(CertCompressionZlibTest, CachedMatchesUncached) {
  // Reference from a separate context, whose first compression is a cache miss
  // and therefore freshly computed.
  RegisteredSsl ref;
  bssl::ScopedCBB ref_cbb;
  ASSERT_EQ(1, CBB_init(ref_cbb.get(), 0));
  ASSERT_EQ(CertCompression::SUCCESS,
            CertCompression::compressZlib(ref.get(), ref_cbb.get(), kTestData, kTestDataLen));
  const std::string reference(reinterpret_cast<const char*>(CBB_data(ref_cbb.get())),
                              CBB_len(ref_cbb.get()));

  RegisteredSsl reg;
  auto compress = [&]() {
    bssl::ScopedCBB cbb;
    EXPECT_EQ(1, CBB_init(cbb.get(), 0));
    EXPECT_EQ(CertCompression::SUCCESS,
              CertCompression::compressZlib(reg.get(), cbb.get(), kTestData, kTestDataLen));
    return std::string(reinterpret_cast<const char*>(CBB_data(cbb.get())), CBB_len(cbb.get()));
  };

  EXPECT_EQ(reference, compress()); // miss: computes and caches
  EXPECT_EQ(reference, compress()); // hit: served from cache
}

TEST(CertCompressionZlibTest, DecompressBadData) {
  constexpr uint8_t bad_compressed_data[2] = {1};
  EXPECT_LOG_CONTAINS(
      "error",
      "Cert zlib decompression failure, possibly caused by invalid compressed cert from peer", {
        CRYPTO_BUFFER* out = nullptr;
        EXPECT_EQ(CertCompression::FAILURE,
                  CertCompression::decompressZlib(nullptr, &out, 100, bad_compressed_data,
                                                  sizeof(bad_compressed_data)));
      });
}

TEST(CertCompressionZlibTest, DecompressBadLength) {
  RegisteredSsl reg;
  bssl::ScopedCBB compressed;
  ASSERT_EQ(1, CBB_init(compressed.get(), 0));
  ASSERT_EQ(CertCompression::SUCCESS,
            CertCompression::compressZlib(reg.get(), compressed.get(), kTestData, kTestDataLen));
  const auto compressed_len = CBB_len(compressed.get());
  EXPECT_GT(compressed_len, 0u);

  EXPECT_LOG_CONTAINS("error",
                      "Zlib decompression length did not match peer provided uncompressed "
                      "length, caused by either invalid peer handshake data or decompression "
                      "error",
                      {
                        CRYPTO_BUFFER* out = nullptr;
                        EXPECT_EQ(CertCompression::FAILURE,
                                  CertCompression::decompressZlib(
                                      nullptr, &out, kTestDataLen + 1 /* intentionally incorrect */,
                                      CBB_data(compressed.get()), compressed_len));
                      });
}

// Two different certificate chains compressed with the same algorithm through the same
// SSL_CTX cache must each round-trip to themselves. The cache is keyed by (algorithm,
// chain), so distinct chains cannot collide; with an algorithm-only key the second chain
// would be served the first chain's compressed bytes.
TEST(CertCompressionBrotliTest, DistinctChainsDoNotCollide) {
  RegisteredSsl reg;
  constexpr uint8_t chain_a[] = {1, 2, 3, 4, 5, 6, 7, 8};
  constexpr uint8_t chain_b[] = {100, 99, 98, 97, 96, 95, 94, 93, 92, 91};

  auto compress = [&](const uint8_t* in, size_t in_len) {
    bssl::ScopedCBB cbb;
    EXPECT_EQ(1, CBB_init(cbb.get(), 0));
    EXPECT_EQ(CertCompression::SUCCESS,
              CertCompression::compressBrotli(reg.get(), cbb.get(), in, in_len));
    return std::string(reinterpret_cast<const char*>(CBB_data(cbb.get())), CBB_len(cbb.get()));
  };
  const std::string compressed_a = compress(chain_a, sizeof(chain_a));
  const std::string compressed_b = compress(chain_b, sizeof(chain_b));

  auto roundtrip = [](const std::string& compressed, size_t uncompressed_len) {
    CRYPTO_BUFFER* out = nullptr;
    EXPECT_EQ(CertCompression::SUCCESS,
              CertCompression::decompressBrotli(nullptr, &out, uncompressed_len,
                                                reinterpret_cast<const uint8_t*>(compressed.data()),
                                                compressed.size()));
    bssl::UniquePtr<CRYPTO_BUFFER> out_ptr(out);
    return std::string(reinterpret_cast<const char*>(CRYPTO_BUFFER_data(out)),
                       CRYPTO_BUFFER_len(out));
  };
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(chain_a), sizeof(chain_a)),
            roundtrip(compressed_a, sizeof(chain_a)));
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(chain_b), sizeof(chain_b)),
            roundtrip(compressed_b, sizeof(chain_b)));
}

//
// End-to-end handshake matrix: two certificates (RSA, ECDSA) x two algorithms.
//

// Generates a fresh RSA-2048 private key.
bssl::UniquePtr<EVP_PKEY> generateRsaKey() {
  bssl::UniquePtr<RSA> rsa(RSA_new());
  bssl::UniquePtr<BIGNUM> e(BN_new());
  RELEASE_ASSERT(BN_set_word(e.get(), RSA_F4) == 1, "");
  RELEASE_ASSERT(RSA_generate_key_ex(rsa.get(), 2048, e.get(), nullptr) == 1, "RSA keygen failed");
  bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());
  RELEASE_ASSERT(EVP_PKEY_assign_RSA(key.get(), rsa.release()) == 1, "");
  return key;
}

// Generates a fresh P-256 ECDSA private key.
bssl::UniquePtr<EVP_PKEY> generateEcdsaKey() {
  bssl::UniquePtr<EC_KEY> ec(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
  RELEASE_ASSERT(ec != nullptr, "");
  RELEASE_ASSERT(EC_KEY_generate_key(ec.get()) == 1, "EC keygen failed");
  bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());
  RELEASE_ASSERT(EVP_PKEY_assign_EC_KEY(key.get(), ec.release()) == 1, "");
  return key;
}

// Builds a self-signed certificate for `key`. The client disables verification, so this is
// only used to give the server a distinct certificate to present and compress.
bssl::UniquePtr<X509> makeSelfSignedCert(EVP_PKEY* key, const std::string& common_name) {
  bssl::UniquePtr<X509> cert(X509_new());
  RELEASE_ASSERT(X509_set_version(cert.get(), 2) == 1, "");
  ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
  X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0);
  X509_gmtime_adj(X509_getm_notAfter(cert.get()), 60 * 60 * 24 * 365);
  RELEASE_ASSERT(X509_set_pubkey(cert.get(), key) == 1, "");
  X509_NAME* name = X509_get_subject_name(cert.get());
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             reinterpret_cast<const uint8_t*>(common_name.c_str()), -1, -1, 0);
  RELEASE_ASSERT(X509_set_issuer_name(cert.get(), name) == 1, "");
  RELEASE_ASSERT(X509_sign(cert.get(), key, EVP_sha256()) > 0, "X509_sign failed");
  return cert;
}

// A TLS 1.3 server context serving one certificate, with both compression algorithms
// registered, so its compressed-cert cache is exercised exactly as in production.
bssl::UniquePtr<SSL_CTX> makeCompressionServerCtx(X509* cert, EVP_PKEY* key) {
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  RELEASE_ASSERT(SSL_CTX_set_min_proto_version(ctx.get(), TLS1_3_VERSION) == 1, "");
  RELEASE_ASSERT(SSL_CTX_set_max_proto_version(ctx.get(), TLS1_3_VERSION) == 1, "");
  RELEASE_ASSERT(SSL_CTX_use_certificate(ctx.get(), cert) == 1, "use_certificate failed");
  RELEASE_ASSERT(SSL_CTX_use_PrivateKey(ctx.get(), key) == 1, "use_PrivateKey failed");
  CertCompression::registerBrotli(ctx.get());
  CertCompression::registerZlib(ctx.get());
  return ctx;
}

using DecompressFn = int (*)(SSL*, CRYPTO_BUFFER**, size_t, const uint8_t*, size_t);

// A TLS 1.3 client context that advertises decompression for exactly one algorithm, so
// the server is forced to use it. Verification is disabled; the test inspects the
// presented certificate directly.
bssl::UniquePtr<SSL_CTX> makeCompressionClientCtx(uint16_t alg, DecompressFn decompress) {
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  RELEASE_ASSERT(SSL_CTX_set_min_proto_version(ctx.get(), TLS1_3_VERSION) == 1, "");
  RELEASE_ASSERT(SSL_CTX_set_max_proto_version(ctx.get(), TLS1_3_VERSION) == 1, "");
  SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_NONE, nullptr);
  RELEASE_ASSERT(
      SSL_CTX_add_cert_compression_alg(ctx.get(), alg, /*compress=*/nullptr, decompress) == 1,
      "add_cert_compression_alg failed");
  return ctx;
}

// Drives an in-memory TLS handshake and returns the server certificate the client
// received, or nullptr if the handshake did not complete.
bssl::UniquePtr<X509> handshakeAndGetServerCert(SSL_CTX* client_ctx, SSL_CTX* server_ctx) {
  bssl::UniquePtr<SSL> client(SSL_new(client_ctx));
  bssl::UniquePtr<SSL> server(SSL_new(server_ctx));
  SSL_set_connect_state(client.get());
  SSL_set_accept_state(server.get());

  BIO* client_bio = nullptr;
  BIO* server_bio = nullptr;
  RELEASE_ASSERT(BIO_new_bio_pair(&client_bio, 1 << 17, &server_bio, 1 << 17) == 1, "bio_pair");
  BIO_up_ref(client_bio);
  SSL_set0_rbio(client.get(), client_bio);
  SSL_set0_wbio(client.get(), client_bio);
  BIO_up_ref(server_bio);
  SSL_set0_rbio(server.get(), server_bio);
  SSL_set0_wbio(server.get(), server_bio);

  for (int i = 0; i < 100; i++) {
    SSL_do_handshake(client.get());
    SSL_do_handshake(server.get());
    if (SSL_is_init_finished(client.get()) && SSL_is_init_finished(server.get())) {
      break;
    }
  }
  if (!SSL_is_init_finished(client.get()) || !SSL_is_init_finished(server.get())) {
    return nullptr;
  }
  return bssl::UniquePtr<X509>(SSL_get_peer_certificate(client.get()));
}

// With the server holding an RSA and an ECDSA certificate (each on its own SSL_CTX, as
// Envoy creates one SSL_CTX per certificate), the client must receive the correct
// certificate for each compression algorithm, across repeated handshakes. The first
// handshake of each (cert, algorithm) pair is a cache miss and the second a cache hit, so
// this exercises the compressed-cert cache end to end.
TEST(CertCompressionMatrixTest, CorrectCertPerAlgorithmAcrossHandshakes) {
  struct CertCase {
    std::string name;
    bssl::UniquePtr<EVP_PKEY> key;
    bssl::UniquePtr<X509> cert;
  };
  std::vector<CertCase> cert_cases;
  {
    bssl::UniquePtr<EVP_PKEY> key = generateRsaKey();
    bssl::UniquePtr<X509> cert = makeSelfSignedCert(key.get(), "rsa.example.com");
    cert_cases.push_back(CertCase{"rsa", std::move(key), std::move(cert)});
  }
  {
    bssl::UniquePtr<EVP_PKEY> key = generateEcdsaKey();
    bssl::UniquePtr<X509> cert = makeSelfSignedCert(key.get(), "ecdsa.example.com");
    cert_cases.push_back(CertCase{"ecdsa", std::move(key), std::move(cert)});
  }

  struct AlgCase {
    std::string name;
    uint16_t id;
    DecompressFn decompress;
  };
  const std::vector<AlgCase> alg_cases = {
      {"brotli", TLSEXT_cert_compression_brotli, CertCompression::decompressBrotli},
      {"zlib", TLSEXT_cert_compression_zlib, CertCompression::decompressZlib},
  };

  for (const auto& cert : cert_cases) {
    // Reused across handshakes so the cache is hit after the first compression.
    bssl::UniquePtr<SSL_CTX> server_ctx = makeCompressionServerCtx(cert.cert.get(), cert.key.get());

    for (const auto& alg : alg_cases) {
      bssl::UniquePtr<SSL_CTX> client_ctx = makeCompressionClientCtx(alg.id, alg.decompress);
      for (int i = 0; i < 2; i++) {
        bssl::UniquePtr<X509> served =
            handshakeAndGetServerCert(client_ctx.get(), server_ctx.get());
        ASSERT_NE(served, nullptr)
            << "handshake failed: cert=" << cert.name << " alg=" << alg.name << " iter=" << i;
        EXPECT_EQ(0, X509_cmp(served.get(), cert.cert.get()))
            << "wrong certificate served: cert=" << cert.name << " alg=" << alg.name
            << " iter=" << i;
      }
    }
  }
}
#endif // ENVOY_SSL_OPENSSL

//
// Registration Tests
// These tests verify that the compression algorithms can be registered with SSL_CTX
//

class CertCompressionRegistrationTest : public testing::Test {
protected:
  void SetUp() override {
    ssl_ctx_.reset(SSL_CTX_new(TLS_method()));
    ASSERT_NE(nullptr, ssl_ctx_.get());
  }

  bssl::UniquePtr<SSL_CTX> ssl_ctx_;
};

TEST_F(CertCompressionRegistrationTest, RegisterBrotli) {
  // Verify brotli registration succeeds without crashing
  EXPECT_NO_THROW(CertCompression::registerBrotli(ssl_ctx_.get()));
}

TEST_F(CertCompressionRegistrationTest, RegisterZlib) {
  // Verify zlib registration succeeds without crashing
  EXPECT_NO_THROW(CertCompression::registerZlib(ssl_ctx_.get()));
}

TEST_F(CertCompressionRegistrationTest, RegisterAllAlgorithms) {
  // Verify all algorithms can be registered on the same context
  // Order matters: brotli > zlib (by priority)
  EXPECT_NO_THROW(CertCompression::registerBrotli(ssl_ctx_.get()));
  EXPECT_NO_THROW(CertCompression::registerZlib(ssl_ctx_.get()));
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
