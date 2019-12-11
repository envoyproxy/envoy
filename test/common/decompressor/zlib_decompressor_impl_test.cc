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

  void testcompressDecompressWithUncommonParams(
      Compressor::ZlibCompressorImpl::CompressionLevel comp_level,
      Compressor::ZlibCompressorImpl::CompressionStrategy comp_strategy, int64_t window_bits,
      uint64_t memory_level) {
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl accumulation_buffer;

    Envoy::Compressor::ZlibCompressorImpl compressor;
    compressor.init(comp_level, comp_strategy, window_bits, memory_level);

    std::string original_text{};
    for (uint64_t i = 0; i < 30; ++i) {
      TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
      original_text.append(buffer.toString());
      compressor.compress(buffer, Compressor::State::Flush);
      accumulation_buffer.add(buffer);
      drainBuffer(buffer);
    }
    ASSERT_EQ(0, buffer.length());

    compressor.compress(buffer, Compressor::State::Finish);
    accumulation_buffer.add(buffer);

    drainBuffer(buffer);
    ASSERT_EQ(0, buffer.length());

    ZlibDecompressorImpl decompressor;
    decompressor.init(window_bits);

    decompressor.decompress(accumulation_buffer, buffer);
    std::string decompressed_text{buffer.toString()};

    ASSERT_EQ(compressor.checksum(), decompressor.checksum());
    ASSERT_EQ(original_text.length(), decompressed_text.length());
    EXPECT_EQ(original_text, decompressed_text);
    ASSERT_EQ(0, decompressor.decompression_error_);
  }

  static const int64_t gzip_window_bits{31};
  static const int64_t memory_level{8};
  static const uint64_t default_input_size{796};
};

class ZlibDecompressorImplFailureTest : public ZlibDecompressorImplTest {
protected:
  static void decompressorBadInitTestHelper(int64_t window_bits) {
    ZlibDecompressorImpl decompressor;
    decompressor.init(window_bits);
  }

  static void uninitializedDecompressorTestHelper() {
    Buffer::OwnedImpl input_buffer;
    Buffer::OwnedImpl output_buffer;
    ZlibDecompressorImpl decompressor;
    TestUtility::feedBufferWithRandomCharacters(input_buffer, 100);
    decompressor.decompress(input_buffer, output_buffer);
    ASSERT_TRUE(decompressor.decompression_error_ < 0);
  }
};

// Test different failures by passing bad initialization params or by calling decompress before
// init.
TEST_F(ZlibDecompressorImplFailureTest, DecompressorFailureTest) {
  EXPECT_DEATH_LOG_TO_STDERR(decompressorBadInitTestHelper(100), "assert failure: result >= 0");
  uninitializedDecompressorTestHelper();
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
  compressor.compress(compressor_buffer, Compressor::State::Flush);
  ASSERT_TRUE(compressor.checksum() > 0);

  ZlibDecompressorImpl decompressor;
  decompressor.init(gzip_window_bits);
  EXPECT_EQ(0, decompressor.checksum());

  decompressor.decompress(compressor_buffer, decompressor_output_buffer);

  drainBuffer(compressor_buffer);
  drainBuffer(decompressor_output_buffer);

  EXPECT_EQ(compressor.checksum(), decompressor.checksum());
  ASSERT_EQ(0, decompressor.decompression_error_);
}

// Exercises compression and decompression by compressing some data, decompressing it and then
// comparing compressor's input/checksum with decompressor's output/checksum.
TEST_F(ZlibDecompressorImplTest, CompressAndDecompress) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;
  Buffer::OwnedImpl empty_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
                  gzip_window_bits, memory_level);

  std::string original_text{};
  for (uint64_t i = 0; i < 20; ++i) {
    TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
    original_text.append(buffer.toString());
    compressor.compress(buffer, Compressor::State::Flush);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);
  }

  ASSERT_EQ(0, buffer.length());

  compressor.compress(buffer, Compressor::State::Finish);
  ASSERT_GE(10, buffer.length());

  accumulation_buffer.add(buffer);

  drainBuffer(buffer);
  ASSERT_EQ(0, buffer.length());

  ZlibDecompressorImpl decompressor;
  decompressor.init(gzip_window_bits);

  decompressor.decompress(accumulation_buffer, buffer);
  std::string decompressed_text{buffer.toString()};

  // Check decompressor's internal state isn't broken.
  drainBuffer(buffer);
  ASSERT_EQ(0, buffer.length());
  decompressor.decompress(empty_buffer, buffer);
  ASSERT_EQ(0, buffer.length());

  ASSERT_EQ(compressor.checksum(), decompressor.checksum());
  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
  ASSERT_EQ(0, decompressor.decompression_error_);
}

// Tests decompression_error_ set to True when Decompression Fails
TEST_F(ZlibDecompressorImplTest, FailedDecompression) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;

  std::string original_text{};
  for (uint64_t i = 0; i < 20; ++i) {
    TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
    original_text.append(buffer.toString());
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);
  }
  ZlibDecompressorImpl decompressor;
  decompressor.init(gzip_window_bits);

  decompressor.decompress(accumulation_buffer, buffer);

  ASSERT_TRUE(decompressor.decompression_error_ < 0);
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
    original_text.append(buffer.toString());
    compressor.compress(buffer, Compressor::State::Flush);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);
  }

  ASSERT_EQ(0, buffer.length());

  compressor.compress(buffer, Compressor::State::Finish);
  ASSERT_GE(10, buffer.length());

  accumulation_buffer.add(buffer);

  drainBuffer(buffer);
  ASSERT_EQ(0, buffer.length());

  ZlibDecompressorImpl decompressor(16);
  decompressor.init(gzip_window_bits);

  decompressor.decompress(accumulation_buffer, buffer);
  std::string decompressed_text{buffer.toString()};

  ASSERT_EQ(compressor.checksum(), decompressor.checksum());
  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
  ASSERT_EQ(0, decompressor.decompression_error_);
}

// Exercises decompression with other supported zlib initialization params.
TEST_F(ZlibDecompressorImplTest, CompressDecompressWithUncommonParams) {
  // Test with different memory levels.
  for (uint64_t i = 1; i < 10; ++i) {
    testcompressDecompressWithUncommonParams(
        Compressor::ZlibCompressorImpl::CompressionLevel::Best,
        Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Rle, 15, i);

    testcompressDecompressWithUncommonParams(
        Compressor::ZlibCompressorImpl::CompressionLevel::Best,
        Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Rle, 15, i);

    testcompressDecompressWithUncommonParams(
        Compressor::ZlibCompressorImpl::CompressionLevel::Speed,
        Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Huffman, 15, i);

    testcompressDecompressWithUncommonParams(
        Compressor::ZlibCompressorImpl::CompressionLevel::Speed,
        Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Filtered, 15, i);
  }
}

TEST_F(ZlibDecompressorImplTest, CompressDecompressOfMultipleSlices) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;

  const std::string sample{"slice, slice, slice, slice, slice, "};
  std::string original_text;
  for (uint64_t i = 0; i < 20; ++i) {
    Buffer::BufferFragmentImpl* frag = new Buffer::BufferFragmentImpl(
        sample.c_str(), sample.size(),
        [](const void*, size_t, const Buffer::BufferFragmentImpl* frag) { delete frag; });

    buffer.addBufferFragment(*frag);
    original_text.append(sample);
  }

  const uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  EXPECT_EQ(num_slices, 20);

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
                  gzip_window_bits, memory_level);

  compressor.compress(buffer, Compressor::State::Flush);
  accumulation_buffer.add(buffer);

  ZlibDecompressorImpl decompressor;
  decompressor.init(gzip_window_bits);

  drainBuffer(buffer);
  ASSERT_EQ(0, buffer.length());

  decompressor.decompress(accumulation_buffer, buffer);
  std::string decompressed_text{buffer.toString()};

  ASSERT_EQ(compressor.checksum(), decompressor.checksum());
  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
}

} // namespace
} // namespace Decompressor
} // namespace Envoy
