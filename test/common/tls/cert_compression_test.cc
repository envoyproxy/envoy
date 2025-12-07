#include "source/common/tls/cert_compression.h"

#include "test/test_common/logging.h"

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
  EXPECT_EQ(kTestDataLen, CRYPTO_BUFFER_len(out));
  EXPECT_EQ(0, memcmp(kTestData, CRYPTO_BUFFER_data(out), kTestDataLen));
}

TEST(CertCompressionBrotliTest, DecompressBadData) {
  EXPECT_LOG_CONTAINS(
      "error",
      "Cert brotli decompression failure, possibly caused by invalid compressed cert from peer",
      {
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

  EXPECT_LOG_CONTAINS("error",
                      "Brotli decompression length did not match peer provided uncompressed length",
                      {
                        CRYPTO_BUFFER* out = nullptr;
                        EXPECT_EQ(CertCompression::FAILURE,
                                  CertCompression::decompressBrotli(
                                      nullptr, &out,
                                      kTestDataLen + 1 /* intentionally incorrect */,
                                      CBB_data(compressed.get()), compressed_len));
                      });
}

//
// Zstd Tests
//

TEST(CertCompressionZstdTest, RoundTrip) {
  // Compress
  bssl::ScopedCBB compressed;
  ASSERT_EQ(1, CBB_init(compressed.get(), 0));
  EXPECT_EQ(CertCompression::SUCCESS,
            CertCompression::compressZstd(nullptr, compressed.get(), kTestData, kTestDataLen));
  const auto compressed_len = CBB_len(compressed.get());
  EXPECT_GT(compressed_len, 0u);

  // Decompress
  CRYPTO_BUFFER* out = nullptr;
  EXPECT_EQ(CertCompression::SUCCESS,
            CertCompression::decompressZstd(nullptr, &out, kTestDataLen,
                                            CBB_data(compressed.get()), compressed_len));
  ASSERT_NE(nullptr, out);
  bssl::UniquePtr<CRYPTO_BUFFER> out_ptr(out);

  // Verify
  EXPECT_EQ(kTestDataLen, CRYPTO_BUFFER_len(out));
  EXPECT_EQ(0, memcmp(kTestData, CRYPTO_BUFFER_data(out), kTestDataLen));
}

TEST(CertCompressionZstdTest, DecompressBadData) {
  EXPECT_LOG_CONTAINS(
      "error",
      "Cert zstd decompression failure, possibly caused by invalid compressed cert from peer",
      {
        CRYPTO_BUFFER* out = nullptr;
        const uint8_t bad_compressed_data = 1;
        EXPECT_EQ(CertCompression::FAILURE,
                  CertCompression::decompressZstd(nullptr, &out, 100, &bad_compressed_data,
                                                  sizeof(bad_compressed_data)));
      });
}

TEST(CertCompressionZstdTest, DecompressBadLength) {
  bssl::ScopedCBB compressed;
  ASSERT_EQ(1, CBB_init(compressed.get(), 0));
  ASSERT_EQ(CertCompression::SUCCESS,
            CertCompression::compressZstd(nullptr, compressed.get(), kTestData, kTestDataLen));
  const auto compressed_len = CBB_len(compressed.get());
  EXPECT_GT(compressed_len, 0u);

  EXPECT_LOG_CONTAINS("error",
                      "Zstd decompression length did not match peer provided uncompressed length",
                      {
                        CRYPTO_BUFFER* out = nullptr;
                        EXPECT_EQ(CertCompression::FAILURE,
                                  CertCompression::decompressZstd(
                                      nullptr, &out,
                                      kTestDataLen + 1 /* intentionally incorrect */,
                                      CBB_data(compressed.get()), compressed_len));
                      });
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
            CertCompression::decompressZlib(nullptr, &out, kTestDataLen,
                                            CBB_data(compressed.get()), compressed_len));
  ASSERT_NE(nullptr, out);
  bssl::UniquePtr<CRYPTO_BUFFER> out_ptr(out);

  // Verify
  EXPECT_EQ(kTestDataLen, CRYPTO_BUFFER_len(out));
  EXPECT_EQ(0, memcmp(kTestData, CRYPTO_BUFFER_data(out), kTestDataLen));
}

TEST(CertCompressionZlibTest, DecompressBadData) {
  EXPECT_LOG_CONTAINS(
      "error",
      "Cert decompression failure in inflate, possibly caused by invalid compressed cert from peer",
      {
        CRYPTO_BUFFER* out = nullptr;
        const uint8_t bad_compressed_data = 1;
        EXPECT_EQ(CertCompression::FAILURE,
                  CertCompression::decompressZlib(nullptr, &out, 100, &bad_compressed_data,
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
                      "Decompression length did not match peer provided uncompressed length, "
                      "caused by either invalid peer handshake data or decompression error.",
                      {
                        CRYPTO_BUFFER* out = nullptr;
                        EXPECT_EQ(CertCompression::FAILURE,
                                  CertCompression::decompressZlib(
                                      nullptr, &out,
                                      kTestDataLen + 1 /* intentionally incorrect */,
                                      CBB_data(compressed.get()), compressed_len));
                      });
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
