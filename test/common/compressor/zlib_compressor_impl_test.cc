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
  void expectValidCompressedBuffer(const Buffer::OwnedImpl& output_buffer,
                                   const uint32_t input_size) {
    uint64_t num_comp_slices = output_buffer.getRawSlices(nullptr, 0);
    Buffer::RawSlice compressed_slices[num_comp_slices];
    output_buffer.getRawSlices(compressed_slices, num_comp_slices);

    const std::string header_hex_str = Hex::encode(
        reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
    // HEADER 0x1f = 31 (window_bits)
    EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
    // CM 0x8 = deflate (compression method)
    EXPECT_EQ("08", header_hex_str.substr(4, 2));

    const std::string footer_bytes_str =
        Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
                    compressed_slices[num_comp_slices - 1].len_);

    expectEqualInputSize(footer_bytes_str, input_size);
  }

  void expectEqualInputSize(const std::string& footer_bytes, const uint32_t input_size) {
    const std::string size_bytes = footer_bytes.substr(footer_bytes.size() - 8, 8);
    uint64_t size;
    StringUtil::atoul(size_bytes.c_str(), size, 16);
    EXPECT_EQ(ntohl(size), input_size);
  }

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

  static void uninitializedCompressorTestHelper() {
    Buffer::OwnedImpl input_buffer;
    Buffer::OwnedImpl output_buffer;
    ZlibCompressorImpl compressor;
    TestUtility::feedBufferWithRandomCharacters(input_buffer, 100);
    compressor.compress(input_buffer, output_buffer);
  }

  static void resetUninitializedCompressorTestHelper() {
    ZlibCompressorImpl compressor;
    compressor.reset();
  }

  static void finishUninitializedCompressorTestHelper() {
    ZlibCompressorImpl compressor;
    Buffer::OwnedImpl output_buffer;
    compressor.finish(output_buffer);
  }
};

/**
 * Exercises death by passing bad initialization params or by calling
 * compress before init.
 */
TEST_F(ZlibCompressorImplDeathTest, CompressorTestDeath) {
  EXPECT_DEATH(compressorBadInitTestHelper(100, 8), std::string{"assert failure: result >= 0"});
  EXPECT_DEATH(compressorBadInitTestHelper(31, 10), std::string{"assert failure: result >= 0"});
  EXPECT_DEATH(uninitializedCompressorTestHelper(), std::string{"assert failure: result == Z_OK"});
  EXPECT_DEATH(resetUninitializedCompressorTestHelper(),
               std::string{"assert failure: result == Z_OK"});
  EXPECT_DEATH(finishUninitializedCompressorTestHelper(),
               std::string{"assert failure: result == Z_STREAM_END"});
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

  const uint32_t input_size = 4096;
  TestUtility::feedBufferWithRandomCharacters(compressor_input_buffer, input_size);
  compressor.compress(compressor_input_buffer, compressor_output_buffer);
  compressor.finish(compressor_output_buffer);
  compressor_input_buffer.drain(input_size);
  EXPECT_TRUE(compressor.checksum() > 0);

  expectValidCompressedBuffer(compressor_output_buffer, input_size);
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

  uint64_t input_size = 0;
  for (uint64_t i = 0; i < 10; i++) {
    TestUtility::feedBufferWithRandomCharacters(input_buffer, default_input_size * i, i);
    compressor.compress(input_buffer, output_buffer);
    input_size += input_buffer.length();
    input_buffer.drain(default_input_size * i);
    ASSERT_EQ(0, input_buffer.length());
  }

  compressor.finish(output_buffer);
  expectValidCompressedBuffer(output_buffer, input_size);
}

/**
 * Exercises compression with a finish call on each received buffer.
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

    compressor.finish(output_buffer);
    expectValidCompressedBuffer(output_buffer, default_input_size * i);

    compressor.reset();
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

  const uint32_t input_size = 10;
  TestUtility::feedBufferWithRandomCharacters(input_buffer, input_size);

  compressor.compress(input_buffer, output_buffer);
  EXPECT_EQ(0, output_buffer.length());

  compressor.finish(output_buffer);
  EXPECT_LE(input_size, output_buffer.length());

  expectValidCompressedBuffer(output_buffer, input_size);
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

  compressor.finish(temp_buffer);
  ASSERT_TRUE(temp_buffer.length() > 0);
  output_buffer.move(temp_buffer);
  const uint64_t first_n_compressed_bytes = output_buffer.length();

  compressor.reset();

  uint64_t input_size = 0;
  for (uint64_t i = 0; i < 15; i++) {
    TestUtility::feedBufferWithRandomCharacters(input_buffer, default_input_size * i, i * 10);
    compressor.compress(input_buffer, temp_buffer);
    input_size += input_buffer.length();
    input_buffer.drain(default_input_size * i);
    ASSERT_EQ(0, input_buffer.length());
    output_buffer.move(temp_buffer);
    ASSERT_EQ(0, temp_buffer.length());
  }

  compressor.finish(temp_buffer);
  output_buffer.move(temp_buffer);
  ASSERT_EQ(0, temp_buffer.length());
  EXPECT_GE(output_buffer.length(), first_n_compressed_bytes);

  expectValidCompressedBuffer(output_buffer, input_size);
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

  uint64_t input_size = 0;
  for (uint64_t i = 0; i < 10; i++) {
    TestUtility::feedBufferWithRandomCharacters(input_buffer, default_input_size * i, i);
    compressor.compress(input_buffer, output_buffer);
    input_size += input_buffer.length();
    input_buffer.drain(default_input_size * i);
    ASSERT_EQ(0, input_buffer.length());
  }

  compressor.finish(output_buffer);
  expectValidCompressedBuffer(output_buffer, input_size);
}

} // namespace
} // namespace Compressor
} // namespace Envoy
