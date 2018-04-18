#include "common/buffer/buffer_impl.h"
#include "common/common/hex.h"
#include "common/compressor/zlib_compressor_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Compressor {
namespace {

class ZlibCompressorImplTest : public testing::Test {
protected:
  static const int64_t gzip_window_bits{31};
  static const int64_t memory_level{8};
  static const uint64_t default_input_size{796};
};

class ZlibCompressorImplDeathTest : public ZlibCompressorImplTest {
protected:
  static void compressorBadInitTestHelper(int64_t window_bits, int64_t mem_level) {
    ZlibCompressorImpl compressor;
    compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
                    ZlibCompressorImpl::CompressionStrategy::Standard, window_bits, mem_level);
  }

  static void unitializedCompressorTestHelper() {
    Buffer::OwnedImpl input_buffer;
    Buffer::OwnedImpl output_buffer;
    ZlibCompressorImpl compressor;
    TestUtility::feedBufferWithRandomCharacters(input_buffer, 100);
    compressor.compress(input_buffer, output_buffer);
  }
};

/**
 * Exercises death by passing bad initialization params or by calling
 * compress before init.
 */
TEST_F(ZlibCompressorImplDeathTest, CompressorTestDeath) {
  EXPECT_DEATH_LOG_TO_STDERR(compressorBadInitTestHelper(100, 8), "assert failure: result >= 0");
  EXPECT_DEATH_LOG_TO_STDERR(compressorBadInitTestHelper(31, 10), "assert failure: result >= 0");
  EXPECT_DEATH_LOG_TO_STDERR(unitializedCompressorTestHelper(), "assert failure: result == Z_OK");
}

/**
 * Exercises compressor's checksum by calling it before init or compress.
 */
TEST_F(ZlibCompressorImplTest, CallingChecksum) {
  Buffer::OwnedImpl compressor_input_buffer;
  Buffer::OwnedImpl compressor_output_buffer;

  ZlibCompressorImpl compressor;
  EXPECT_EQ(0, compressor.checksum());

  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
                  gzip_window_bits, memory_level);
  EXPECT_EQ(0, compressor.checksum());

  TestUtility::feedBufferWithRandomCharacters(compressor_input_buffer, 4096);
  compressor.compress(compressor_input_buffer, compressor_output_buffer);
  compressor.flush(compressor_output_buffer);
  compressor_input_buffer.drain(4096);
  EXPECT_TRUE(compressor.checksum() > 0);
}

/**
 * Exercises compression with a very small output buffer.
 */
TEST_F(ZlibCompressorImplTest, CompressWithReducedInternalMemory) {
  Buffer::OwnedImpl input_buffer;
  Buffer::OwnedImpl output_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor(8);
  compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
                  ZlibCompressorImpl::CompressionStrategy::Standard, gzip_window_bits,
                  memory_level);

  for (uint64_t i = 0; i < 10; i++) {
    TestUtility::feedBufferWithRandomCharacters(input_buffer, default_input_size * i, i);
    compressor.compress(input_buffer, output_buffer);
    input_buffer.drain(default_input_size * i);
    ASSERT_EQ(0, input_buffer.length());
  }

  compressor.flush(output_buffer);

  uint64_t num_comp_slices = output_buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice compressed_slices[num_comp_slices];
  output_buffer.getRawSlices(compressed_slices, num_comp_slices);

  const std::string header_hex_str = Hex::encode(
      reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
  // CM 0x8 = deflate (compression method)
  EXPECT_EQ("08", header_hex_str.substr(4, 2));

  const std::string footer_hex_str =
      Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
                  compressed_slices[num_comp_slices - 1].len_);
  // FOOTER four-byte sequence (sync flush)
  EXPECT_EQ("0000ffff", footer_hex_str.substr(footer_hex_str.size() - 8, 10));
}

/**
 * Exercises compression with a flush call on each received buffer.
 */
TEST_F(ZlibCompressorImplTest, CompressWithContinuesFlush) {
  Buffer::OwnedImpl input_buffer;
  Buffer::OwnedImpl output_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
                  ZlibCompressorImpl::CompressionStrategy::Standard, gzip_window_bits,
                  memory_level);

  for (uint64_t i = 0; i < 10; i++) {
    TestUtility::feedBufferWithRandomCharacters(input_buffer, default_input_size * i, i);
    compressor.compress(input_buffer, output_buffer);

    input_buffer.drain(default_input_size * i);
    ASSERT_EQ(0, input_buffer.length());

    compressor.flush(output_buffer);

    uint64_t num_comp_slices = output_buffer.getRawSlices(nullptr, 0);
    Buffer::RawSlice compressed_slices[num_comp_slices];
    output_buffer.getRawSlices(compressed_slices, num_comp_slices);

    const std::string header_hex_str = Hex::encode(
        reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
    // HEADER 0x1f = 31 (window_bits)
    EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
    // CM 0x8 = deflate (compression method)
    EXPECT_EQ("08", header_hex_str.substr(4, 2));

    const std::string footer_hex_str =
        Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
                    compressed_slices[num_comp_slices - 1].len_);
    // FOOTER four-byte sequence (sync flush)
    EXPECT_EQ("0000ffff", footer_hex_str.substr(footer_hex_str.size() - 8, 10));
  }
}

/**
 * Exercises compression with very small input buffer.
 */
TEST_F(ZlibCompressorImplTest, CompressSmallInputMemory) {
  Buffer::OwnedImpl input_buffer;
  Buffer::OwnedImpl output_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
                  ZlibCompressorImpl::CompressionStrategy::Standard, gzip_window_bits,
                  memory_level);

  TestUtility::feedBufferWithRandomCharacters(input_buffer, 10);

  compressor.compress(input_buffer, output_buffer);
  EXPECT_EQ(0, output_buffer.length());

  compressor.flush(output_buffer);
  EXPECT_LE(10, output_buffer.length());

  uint64_t num_comp_slices = output_buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice compressed_slices[num_comp_slices];
  output_buffer.getRawSlices(compressed_slices, num_comp_slices);

  const std::string header_hex_str = Hex::encode(
      reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
  // CM 0x8 = deflate (compression method)
  EXPECT_EQ("08", header_hex_str.substr(4, 2));

  const std::string footer_hex_str =
      Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
                  compressed_slices[num_comp_slices - 1].len_);
  // FOOTER four-byte sequence (sync flush)
  EXPECT_EQ("0000ffff", footer_hex_str.substr(footer_hex_str.size() - 8, 10));
}

/**
 * Exercises common flow of compressing some data, making it available to output buffer,
 * then moving output buffer to another buffer and so on.
 */
TEST_F(ZlibCompressorImplTest, CompressMoveFlushAndRepeat) {
  Buffer::OwnedImpl input_buffer;
  Buffer::OwnedImpl temp_buffer;
  Buffer::OwnedImpl output_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
                  ZlibCompressorImpl::CompressionStrategy::Standard, gzip_window_bits,
                  memory_level);

  for (uint64_t i = 0; i < 20; i++) {
    TestUtility::feedBufferWithRandomCharacters(input_buffer, default_input_size * i, i);
    compressor.compress(input_buffer, temp_buffer);
    input_buffer.drain(default_input_size * i);
    ASSERT_EQ(0, input_buffer.length());
    output_buffer.move(temp_buffer);
    ASSERT_EQ(0, temp_buffer.length());
  }

  compressor.flush(temp_buffer);
  ASSERT_TRUE(temp_buffer.length() > 0);
  output_buffer.move(temp_buffer);
  const uint64_t first_n_compressed_bytes = output_buffer.length();

  for (uint64_t i = 0; i < 15; i++) {
    TestUtility::feedBufferWithRandomCharacters(input_buffer, default_input_size * i, i * 10);
    compressor.compress(input_buffer, temp_buffer);
    input_buffer.drain(default_input_size * i);
    ASSERT_EQ(0, input_buffer.length());
    output_buffer.move(temp_buffer);
    ASSERT_EQ(0, temp_buffer.length());
  }

  compressor.flush(temp_buffer);
  output_buffer.move(temp_buffer);
  ASSERT_EQ(0, temp_buffer.length());
  EXPECT_GE(output_buffer.length(), first_n_compressed_bytes);

  uint64_t num_comp_slices = output_buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice compressed_slices[num_comp_slices];
  output_buffer.getRawSlices(compressed_slices, num_comp_slices);

  const std::string header_hex_str = Hex::encode(
      reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
  // CM 0x8 = deflate (compression method)
  EXPECT_EQ("08", header_hex_str.substr(4, 2));

  const std::string footer_hex_str =
      Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
                  compressed_slices[num_comp_slices - 1].len_);
  // FOOTER four-byte sequence (sync flush)
  EXPECT_EQ("0000ffff", footer_hex_str.substr(footer_hex_str.size() - 8, 10));
}

/**
 * Exercises compression with other supported zlib initialization params.
 */
TEST_F(ZlibCompressorImplTest, CompressWithNotCommonParams) {
  Buffer::OwnedImpl input_buffer;
  Buffer::OwnedImpl output_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(ZlibCompressorImpl::CompressionLevel::Speed,
                  ZlibCompressorImpl::CompressionStrategy::Rle, gzip_window_bits, 1);

  for (uint64_t i = 0; i < 10; i++) {
    TestUtility::feedBufferWithRandomCharacters(input_buffer, default_input_size * i, i);
    compressor.compress(input_buffer, output_buffer);
    input_buffer.drain(default_input_size * i);
    ASSERT_EQ(0, input_buffer.length());
  }

  compressor.flush(output_buffer);

  uint64_t num_comp_slices = output_buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice compressed_slices[num_comp_slices];
  output_buffer.getRawSlices(compressed_slices, num_comp_slices);

  const std::string header_hex_str = Hex::encode(
      reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
  // CM 0x8 = deflate (compression method)
  EXPECT_EQ("08", header_hex_str.substr(4, 2));

  const std::string footer_hex_str =
      Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
                  compressed_slices[num_comp_slices - 1].len_);
  // FOOTER four-byte sequence (sync flush)
  EXPECT_EQ("0000ffff", footer_hex_str.substr(footer_hex_str.size() - 8, 10));
}

} // namespace
} // namespace Compressor
} // namespace Envoy
