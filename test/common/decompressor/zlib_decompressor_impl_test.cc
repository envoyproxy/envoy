#include "common/buffer/buffer_impl.h"
#include "common/common/hex.h"
#include "common/compressor/zlib_compressor_impl.h"
#include "common/decompressor/zlib_decompressor_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Decompressor {
namespace {

class ZlibDecompressorImplTest : public testing::Test {
protected:
  static const int64_t gzip_window_bits{31};
  static const int64_t memory_level{8};
  static const uint64_t default_input_size{796};
};

class ZlibDecompressorImplDeathTest : public ZlibDecompressorImplTest {
protected:
  static void decompressorBadInitTestHelper(int64_t window_bits) {
    ZlibDecompressorImpl decompressor;
    decompressor.init(window_bits);
  }

  static void unitializedDecompressorTestHelper() {
    Buffer::OwnedImpl input_buffer;
    Buffer::OwnedImpl ouput_buffer;
    ZlibDecompressorImpl decompressor;
    TestUtility::feedBufferWithRandomCharacters(input_buffer, 100);
    decompressor.decompress(input_buffer, ouput_buffer);
  }
};

/**
 * Exercises death by passing bad initialization params or by calling
 * decompress before init.
 */
TEST_F(ZlibDecompressorImplDeathTest, DecompressorTestDeath) {
  EXPECT_DEATH_LOG_TO_STDERR(decompressorBadInitTestHelper(100), "assert failure: result >= 0");
  EXPECT_DEATH_LOG_TO_STDERR(unitializedDecompressorTestHelper(), "assert failure: result == Z_OK");
}

/**
 * Exercises decompressor's checksum by calling it before init or decompress.
 */
TEST_F(ZlibDecompressorImplTest, CallingChecksum) {
  Buffer::OwnedImpl compressor_input_buffer;
  Buffer::OwnedImpl compressor_output_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  ASSERT_EQ(0, compressor.checksum());

  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
                  gzip_window_bits, memory_level);
  ASSERT_EQ(0, compressor.checksum());

  TestUtility::feedBufferWithRandomCharacters(compressor_input_buffer, 4096);
  compressor.compress(compressor_input_buffer, compressor_output_buffer);
  compressor.flush(compressor_output_buffer);
  compressor_input_buffer.drain(4096);
  ASSERT_TRUE(compressor.checksum() > 0);

  ZlibDecompressorImpl decompressor;
  decompressor.init(gzip_window_bits);
  EXPECT_EQ(0, decompressor.checksum());

  // compressor_output_buffer becomes decompressor input param.
  // compressor_input_buffer is re-used as decompressor output since it is empty.
  decompressor.decompress(compressor_output_buffer, compressor_input_buffer);
  EXPECT_EQ(compressor.checksum(), decompressor.checksum());
}

/**
 * Exercises compression and decompression by compressing some data, decompressing it and then
 * comparing compressor's input/checksum with decompressor's output/checksum.
 */
TEST_F(ZlibDecompressorImplTest, CompressAndDecompress) {
  Buffer::OwnedImpl compressor_input_buffer;
  Buffer::OwnedImpl compressor_output_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
                  gzip_window_bits, memory_level);

  std::string original_text{};
  for (uint64_t i = 0; i < 20; ++i) {
    TestUtility::feedBufferWithRandomCharacters(compressor_input_buffer, default_input_size * i, i);
    compressor.compress(compressor_input_buffer, compressor_output_buffer);
    original_text.append(TestUtility::bufferToString(compressor_input_buffer));
    compressor_input_buffer.drain(default_input_size * i);
  }

  compressor.flush(compressor_output_buffer);

  ZlibDecompressorImpl decompressor;
  decompressor.init(gzip_window_bits);
  ASSERT_EQ(0, compressor_input_buffer.length());

  // compressor_output_buffer becomes decompressor input param.
  // compressor_input_buffer is re-used as decompressor output since it is empty.
  decompressor.decompress(compressor_output_buffer, compressor_input_buffer);

  std::string decompressed_text{TestUtility::bufferToString(compressor_input_buffer)};

  ASSERT_EQ(compressor.checksum(), decompressor.checksum());
  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
}

/**
 * Exercises decompression with a very small output buffer.
 */
TEST_F(ZlibDecompressorImplTest, DecompressWithSmallOutputBuffer) {
  Buffer::OwnedImpl input_buffer;
  Buffer::OwnedImpl output_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
                  gzip_window_bits, memory_level);

  std::string original_text{};
  for (uint64_t i = 0; i < 20; ++i) {
    TestUtility::feedBufferWithRandomCharacters(input_buffer, default_input_size * i, i);
    compressor.compress(input_buffer, output_buffer);
    original_text.append(TestUtility::bufferToString(input_buffer));
    input_buffer.drain(default_input_size * i);
  }
  compressor.flush(output_buffer);

  ZlibDecompressorImpl decompressor(16);
  decompressor.init(gzip_window_bits);
  ASSERT_EQ(0, input_buffer.length());
  decompressor.decompress(output_buffer, input_buffer);

  std::string decompressed_text{TestUtility::bufferToString(input_buffer)};

  ASSERT_EQ(compressor.checksum(), decompressor.checksum());
  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
}

/**
 * Exercises decompression with other supported zlib initialization params.
 */
TEST_F(ZlibDecompressorImplTest, CompressDecompressWithUncommonParams) {
  Buffer::OwnedImpl input_buffer;
  Buffer::OwnedImpl output_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Best,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Rle, 15, 2);

  std::string original_text{};
  for (uint64_t i = 0; i < 20; ++i) {
    TestUtility::feedBufferWithRandomCharacters(input_buffer, default_input_size * i, i);
    compressor.compress(input_buffer, output_buffer);
    original_text.append(TestUtility::bufferToString(input_buffer));
    input_buffer.drain(default_input_size * i);
  }
  compressor.flush(output_buffer);

  ZlibDecompressorImpl decompressor;
  decompressor.init(15);
  ASSERT_EQ(0, input_buffer.length());
  decompressor.decompress(output_buffer, input_buffer);

  std::string decompressed_text{TestUtility::bufferToString(input_buffer)};

  ASSERT_EQ(compressor.checksum(), decompressor.checksum());
  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
}

} // namespace
} // namespace Decompressor
} // namespace Envoy
