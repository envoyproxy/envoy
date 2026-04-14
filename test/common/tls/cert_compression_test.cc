#include <functional>

#include "source/common/tls/cert_compression.h"

#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/types/span.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// Test data for round-trip compression tests
constexpr uint8_t kTestData[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
constexpr size_t kTestDataLen = sizeof(kTestData);

//
// Brotli Tests
//

TEST(CertCompressionBrotliTest, RoundTrip) {
  // Compress
  bssl::ScopedCBB compressed;
  ASSERT_EQ(1, CBB_init(compressed.get(), 0));
  EXPECT_EQ(CertCompression::SUCCESS,
            CertCompression::compressBrotli(nullptr, compressed.get(), kTestData, kTestDataLen));
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
  bssl::ScopedCBB compressed;
  ASSERT_EQ(1, CBB_init(compressed.get(), 0));
  ASSERT_EQ(CertCompression::SUCCESS,
            CertCompression::compressBrotli(nullptr, compressed.get(), kTestData, kTestDataLen));
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

TEST(CertCompressionBrotliTest, CompressHugeInputSizeReturnsFailure) {
  // BrotliEncoderMaxCompressedSize returns 0 for input sizes > ~2^30.
  // This triggers the error path at lines 62-65 in cert_compression.cc.
  bssl::ScopedCBB compressed;
  ASSERT_EQ(1, CBB_init(compressed.get(), 0));
  EXPECT_ENVOY_BUG(CertCompression::compressBrotli(nullptr, compressed.get(), nullptr, 1 << 31),
                   "BrotliEncoderMaxCompressedSize returned 0");
}

//
// Zlib Tests
//

TEST(CertCompressionZlibTest, RoundTrip) {
  // Compress
  bssl::ScopedCBB compressed;
  ASSERT_EQ(1, CBB_init(compressed.get(), 0));
  EXPECT_EQ(CertCompression::SUCCESS,
            CertCompression::compressZlib(nullptr, compressed.get(), kTestData, kTestDataLen));
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
  bssl::ScopedCBB compressed;
  ASSERT_EQ(1, CBB_init(compressed.get(), 0));
  ASSERT_EQ(CertCompression::SUCCESS,
            CertCompression::compressZlib(nullptr, compressed.get(), kTestData, kTestDataLen));
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

//
// Verify cert compression actually happens on the wire during the handshake
//

namespace {

// Performs a TLS handshake over a BIO pair and returns the number of bytes
// written by the server. Returns 0 on handshake failure.
size_t doHandshakeAndCountServerBytes(SSL_CTX* client_ctx, SSL_CTX* server_ctx) {
  bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx));
  SSL_set_accept_state(server_ssl.get());

  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx));
  SSL_set_connect_state(client_ssl.get());

  BIO *client_bio, *server_bio;
  if (!BIO_new_bio_pair(&client_bio, 16384, &server_bio, 16384)) {
    return 0;
  }
  BIO_up_ref(client_bio);
  BIO_up_ref(server_bio);
  SSL_set0_rbio(client_ssl.get(), client_bio);
  SSL_set0_wbio(client_ssl.get(), client_bio);
  SSL_set0_rbio(server_ssl.get(), server_bio);
  SSL_set0_wbio(server_ssl.get(), server_bio);

  bool handshake_complete = false;
  for (int i = 0; i < 100 && !handshake_complete; i++) {
    SSL_do_handshake(client_ssl.get());
    int ret = SSL_do_handshake(server_ssl.get());
    if (ret == 1) {
      SSL_do_handshake(client_ssl.get());
      handshake_complete = true;
    }
  }

  if (!handshake_complete) {
    return 0;
  }

  EXPECT_STREQ("TLSv1.3", SSL_get_version(server_ssl.get()));

  return BIO_number_written(server_bio);
}

// Performs an uncompressed and then compressed handshake and verifies that the
// expected reduction in bytes is observed on the wire in the compressed case.
void verifyCompressionReducesBytes(void (*register_server_compression)(SSL_CTX*),
                                   void (*register_client_compression)(SSL_CTX*),
                                   void (*check_bytes_saved)(int bytes)) {
  std::string cert_path =
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem");
  std::string key_path =
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem");

  // Server context with cert and key (shared for both handshakes)
  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_method()));
  ASSERT_NE(nullptr, server_ctx.get());
  ASSERT_EQ(1, SSL_CTX_use_certificate_file(server_ctx.get(), cert_path.c_str(), SSL_FILETYPE_PEM));
  ASSERT_EQ(1, SSL_CTX_use_PrivateKey_file(server_ctx.get(), key_path.c_str(), SSL_FILETYPE_PEM));

  // Uncompressed handshake
  bssl::UniquePtr<SSL_CTX> client_ctx_uncompressed(SSL_CTX_new(TLS_method()));
  ASSERT_NE(nullptr, client_ctx_uncompressed.get());

  size_t uncompressed_bytes =
      doHandshakeAndCountServerBytes(client_ctx_uncompressed.get(), server_ctx.get());
  ASSERT_GT(uncompressed_bytes, 0u);

  // Register compression on server context
  register_server_compression(server_ctx.get());

  bssl::UniquePtr<SSL_CTX> client_ctx_compressed(SSL_CTX_new(TLS_method()));
  ASSERT_NE(nullptr, client_ctx_compressed.get());
  register_client_compression(client_ctx_compressed.get());

  size_t compressed_bytes =
      doHandshakeAndCountServerBytes(client_ctx_compressed.get(), server_ctx.get());
  ASSERT_GT(compressed_bytes, 0u);

  check_bytes_saved(uncompressed_bytes - compressed_bytes);
}

// Based on the test certificate used in verifyCompressionReducesBytes(), these
// are the expected number of bytes saved when compression is performed with
// brotli or zlib respectively. Although the absolute number of bytes on the
// wire will differ between BoringSSL and OpenSSL implemetations, because of
// slightly different extensions etc, the number of bytes saved by applying
// compression to the certificate, will be the same in both cases.
const int BROTLI_BYTES_SAVED = 209;
const int ZLIB_BYTES_SAVED = 160;

} // namespace

TEST(CertCompressionWireTest, BrotliReducesBytesOnWire) {
  verifyCompressionReducesBytes(
      [](SSL_CTX* ctx) { CertCompression::registerBrotli(ctx); },
      [](SSL_CTX* ctx) { CertCompression::registerBrotli(ctx); },
      [](int bytes_saved) { EXPECT_EQ(BROTLI_BYTES_SAVED, bytes_saved); });
}

TEST(CertCompressionWireTest, ZlibReducesBytesOnWire) {
  verifyCompressionReducesBytes([](SSL_CTX* ctx) { CertCompression::registerZlib(ctx); },
                                [](SSL_CTX* ctx) { CertCompression::registerZlib(ctx); },
                                [](int bytes_saved) { EXPECT_EQ(ZLIB_BYTES_SAVED, bytes_saved); });
}

TEST(CertCompressionWireTest, BothRegisteredBrotliFirstPrefersBrotli) {
  verifyCompressionReducesBytes(
      [](SSL_CTX* ctx) {
        CertCompression::registerBrotli(ctx);
        CertCompression::registerZlib(ctx);
      },
      [](SSL_CTX* ctx) {
        CertCompression::registerBrotli(ctx);
        CertCompression::registerZlib(ctx);
      },
      [](int bytes_saved) { EXPECT_EQ(BROTLI_BYTES_SAVED, bytes_saved); });
}

TEST(CertCompressionWireTest, BothRegisteredZlibFirstPrefersZlib) {
  verifyCompressionReducesBytes(
      [](SSL_CTX* ctx) {
        CertCompression::registerZlib(ctx);
        CertCompression::registerBrotli(ctx);
      },
      [](SSL_CTX* ctx) {
        CertCompression::registerZlib(ctx);
        CertCompression::registerBrotli(ctx);
      },
      [](int bytes_saved) { EXPECT_EQ(ZLIB_BYTES_SAVED, bytes_saved); });
}

TEST(CertCompressionWireTest, DifferentClientServerOrderPrefersEither) {
  verifyCompressionReducesBytes(
      [](SSL_CTX* ctx) {
        CertCompression::registerZlib(ctx);
        CertCompression::registerBrotli(ctx);
      },
      [](SSL_CTX* ctx) {
        CertCompression::registerBrotli(ctx);
        CertCompression::registerZlib(ctx);
      },
      [](int bytes_saved) {
        EXPECT_THAT(bytes_saved,
                    testing::AnyOf(testing::Eq(ZLIB_BYTES_SAVED), testing::Eq(BROTLI_BYTES_SAVED)));
      });

  verifyCompressionReducesBytes(
      [](SSL_CTX* ctx) {
        CertCompression::registerBrotli(ctx);
        CertCompression::registerZlib(ctx);
      },
      [](SSL_CTX* ctx) {
        CertCompression::registerZlib(ctx);
        CertCompression::registerBrotli(ctx);
      },
      [](int bytes_saved) {
        EXPECT_THAT(bytes_saved,
                    testing::AnyOf(testing::Eq(ZLIB_BYTES_SAVED), testing::Eq(BROTLI_BYTES_SAVED)));
      });
}

TEST(CertCompressionWireTest, NegotiatesCommonAlgorithmClientSubset) {
  verifyCompressionReducesBytes(
      [](SSL_CTX* ctx) {
        CertCompression::registerBrotli(ctx);
        CertCompression::registerZlib(ctx);
      },
      [](SSL_CTX* ctx) { CertCompression::registerZlib(ctx); },
      [](int bytes_saved) { EXPECT_EQ(ZLIB_BYTES_SAVED, bytes_saved); });

  verifyCompressionReducesBytes(
      [](SSL_CTX* ctx) {
        CertCompression::registerZlib(ctx);
        CertCompression::registerBrotli(ctx);
      },
      [](SSL_CTX* ctx) { CertCompression::registerZlib(ctx); },
      [](int bytes_saved) { EXPECT_EQ(ZLIB_BYTES_SAVED, bytes_saved); });
}

TEST(CertCompressionWireTest, NegotiatesCommonAlgorithmServerSubset) {
  verifyCompressionReducesBytes([](SSL_CTX* ctx) { CertCompression::registerZlib(ctx); },
                                [](SSL_CTX* ctx) {
                                  CertCompression::registerBrotli(ctx);
                                  CertCompression::registerZlib(ctx);
                                },
                                [](int bytes_saved) { EXPECT_EQ(ZLIB_BYTES_SAVED, bytes_saved); });

  verifyCompressionReducesBytes([](SSL_CTX* ctx) { CertCompression::registerZlib(ctx); },
                                [](SSL_CTX* ctx) {
                                  CertCompression::registerZlib(ctx);
                                  CertCompression::registerBrotli(ctx);
                                },
                                [](int bytes_saved) { EXPECT_EQ(ZLIB_BYTES_SAVED, bytes_saved); });
}

TEST(CertCompressionWireTest, NoCommonAlgorithmNoCompression) {
  verifyCompressionReducesBytes([](SSL_CTX* ctx) { CertCompression::registerBrotli(ctx); },
                                [](SSL_CTX* ctx) { CertCompression::registerZlib(ctx); },
                                [](int bytes_saved) { EXPECT_EQ(0, bytes_saved); });

  verifyCompressionReducesBytes([](SSL_CTX* ctx) { CertCompression::registerZlib(ctx); },
                                [](SSL_CTX* ctx) { CertCompression::registerBrotli(ctx); },
                                [](int bytes_saved) { EXPECT_EQ(0, bytes_saved); });
}

TEST(CertCompressionWireTest, NoClientCompressionNoCompression) {
  verifyCompressionReducesBytes(
      [](SSL_CTX* ctx) {
        CertCompression::registerBrotli(ctx);
        CertCompression::registerZlib(ctx);
      },
      [](SSL_CTX*) {}, [](int bytes_saved) { EXPECT_EQ(0, bytes_saved); });
}

TEST(CertCompressionWireTest, NoServerCompressionNoCompression) {
  verifyCompressionReducesBytes([](SSL_CTX*) {},
                                [](SSL_CTX* ctx) {
                                  CertCompression::registerBrotli(ctx);
                                  CertCompression::registerZlib(ctx);
                                },
                                [](int bytes_saved) { EXPECT_EQ(0, bytes_saved); });
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
