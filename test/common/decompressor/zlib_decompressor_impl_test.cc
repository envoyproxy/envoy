#include <string>

#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
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
  static const int8_t gzip_window_bits{31};
  static const int8_t memory_level{8};
  static const uint64_t default_input_size{796};
};

class ZlibDecompressorImplDeathTest : public ZlibDecompressorImplTest {
protected:
  static void decompressorBadInitTestHelper(int8_t window_bits) {
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

TEST_F(ZlibDecompressorImplDeathTest, DecompressorTestDeath) {
  EXPECT_DEATH(decompressorBadInitTestHelper(100), std::string{"assert failure: result >= 0"});
  EXPECT_DEATH(unitializedDecompressorTestHelper(), std::string{"assert failure: result == Z_OK"});
}

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

TEST_F(ZlibDecompressorImplTest, DecompressWithReducedInternalMemory) {
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
