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
  void drainBuffer(Buffer::OwnedImpl& buffer) { buffer.drain(buffer.length()); }

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

// Exercises death by passing bad initialization params or by calling decompress before init.
TEST_F(ZlibDecompressorImplDeathTest, DecompressorTestDeath) {
  EXPECT_DEATH(decompressorBadInitTestHelper(100), std::string{"assert failure: result >= 0"});
  EXPECT_DEATH(unitializedDecompressorTestHelper(), std::string{"assert failure: result == Z_OK"});
}

// Exercises decompressor's checksum by calling it before init or decompress.
TEST_F(ZlibDecompressorImplTest, CallingChecksum) {
  Buffer::OwnedImpl compressor_buffer;
  Buffer::OwnedImpl decompressor_output_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  ASSERT_EQ(0, compressor.checksum());

  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
                  gzip_window_bits, memory_level);
  ASSERT_EQ(0, compressor.checksum());

  TestUtility::feedBufferWithRandomCharacters(compressor_buffer, 4096);
  compressor.compress(compressor_buffer, false);
  ASSERT_TRUE(compressor.checksum() > 0);

  ZlibDecompressorImpl decompressor;
  decompressor.init(gzip_window_bits);
  EXPECT_EQ(0, decompressor.checksum());

  decompressor.decompress(compressor_buffer, decompressor_output_buffer);

  drainBuffer(compressor_buffer);
  drainBuffer(decompressor_output_buffer);

  EXPECT_EQ(compressor.checksum(), decompressor.checksum());
}

// Exercises compression and decompression by compressing some data, decompressing it and then
// comparing compressor's input/checksum with decompressor's output/checksum.
TEST_F(ZlibDecompressorImplTest, CompressAndDecompress) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
                  gzip_window_bits, memory_level);

  std::string original_text{};
  for (uint64_t i = 0; i < 20; ++i) {
    TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
    original_text.append(TestUtility::bufferToString(buffer));
    compressor.compress(buffer, false);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);
  }

  ASSERT_EQ(0, buffer.length());

  compressor.compress(buffer, true);
  ASSERT_GE(10, buffer.length());

  accumulation_buffer.add(buffer);

  drainBuffer(buffer);
  ASSERT_EQ(0, buffer.length());

  ZlibDecompressorImpl decompressor;
  decompressor.init(gzip_window_bits);

  decompressor.decompress(accumulation_buffer, buffer);
  std::string decompressed_text{TestUtility::bufferToString(buffer)};

  ASSERT_EQ(compressor.checksum(), decompressor.checksum());
  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
}

// Exercises decompression with a very small output buffer.
TEST_F(ZlibDecompressorImplTest, DecompressWithSmallOutputBuffer) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
                  gzip_window_bits, memory_level);

  std::string original_text{};
  for (uint64_t i = 0; i < 20; ++i) {
    TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
    original_text.append(TestUtility::bufferToString(buffer));
    compressor.compress(buffer, false);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);
  }

  ASSERT_EQ(0, buffer.length());

  compressor.compress(buffer, true);
  ASSERT_GE(10, buffer.length());

  accumulation_buffer.add(buffer);

  drainBuffer(buffer);
  ASSERT_EQ(0, buffer.length());

  ZlibDecompressorImpl decompressor(16);
  decompressor.init(gzip_window_bits);

  decompressor.decompress(accumulation_buffer, buffer);
  std::string decompressed_text{TestUtility::bufferToString(buffer)};

  ASSERT_EQ(compressor.checksum(), decompressor.checksum());
  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
}

// Exercises decompression with other supported zlib initialization params.
TEST_F(ZlibDecompressorImplTest, CompressDecompressWithUncommonParams) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Best,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Rle, 15, 1);

  std::string original_text{};
  for (uint64_t i = 0; i < 5; ++i) {
    TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
    original_text.append(TestUtility::bufferToString(buffer));
    compressor.compress(buffer, false);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);
  }

  ASSERT_EQ(0, buffer.length());

  compressor.compress(buffer, true);
  ASSERT_GE(10, buffer.length());

  accumulation_buffer.add(buffer);

  drainBuffer(buffer);
  ASSERT_EQ(0, buffer.length());

  ZlibDecompressorImpl decompressor;
  decompressor.init(15);

  decompressor.decompress(accumulation_buffer, buffer);
  std::string decompressed_text{TestUtility::bufferToString(buffer)};

  ASSERT_EQ(compressor.checksum(), decompressor.checksum());
  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
}

} // namespace
} // namespace Decompressor
} // namespace Envoy
