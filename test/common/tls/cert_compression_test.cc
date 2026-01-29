#include "source/common/tls/cert_compression.h"

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

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
