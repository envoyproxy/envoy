#include "source/common/quic/cert_compression.h"

#include "test/test_common/logging.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

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
  constexpr uint8_t the_data[] = {1, 2, 3, 4, 5, 6};
  constexpr size_t uncompressed_len = 6;
  bssl::ScopedCBB compressed;
  ASSERT_EQ(1, CBB_init(compressed.get(), 0));
  ASSERT_EQ(CertCompression::SUCCESS,
            CertCompression::compressZlib(nullptr, compressed.get(), the_data, uncompressed_len));
  const auto compressed_len = CBB_len(compressed.get());
  EXPECT_NE(0, compressed_len);

  EXPECT_LOG_CONTAINS("error",
                      "Decompression length did not match peer provided uncompressed length, "
                      "caused by either invalid peer handshake data or decompression error.",
                      {
                        CRYPTO_BUFFER* out = nullptr;
                        EXPECT_EQ(CertCompression::FAILURE,
                                  CertCompression::decompressZlib(
                                      nullptr, &out,
                                      uncompressed_len + 1 /* intentionally incorrect */,
                                      CBB_data(compressed.get()), compressed_len));
                      });
}
} // namespace Quic
} // namespace Envoy
