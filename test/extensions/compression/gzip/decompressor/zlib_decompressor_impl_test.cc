#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hex.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/compression/gzip/compressor/zlib_compressor_impl.h"
#include "source/extensions/compression/gzip/decompressor/zlib_decompressor_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Gzip {
namespace Decompressor {

class ZlibDecompressorImplTest : public testing::Test {
protected:
  void drainBuffer(Buffer::OwnedImpl& buffer) { buffer.drain(buffer.length()); }

  void testcompressDecompressWithUncommonParams(
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel comp_level,
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy
          comp_strategy,
      int64_t window_bits, uint64_t memory_level) {
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl accumulation_buffer;

    Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl compressor;
    compressor.init(comp_level, comp_strategy, window_bits, memory_level);

    std::string original_text{};
    for (uint64_t i = 0; i < 30; ++i) {
      TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
      original_text.append(buffer.toString());
      compressor.compress(buffer, Envoy::Compression::Compressor::State::Flush);
      accumulation_buffer.add(buffer);
      drainBuffer(buffer);
    }
    ASSERT_EQ(0, buffer.length());

    compressor.compress(buffer, Envoy::Compression::Compressor::State::Finish);
    accumulation_buffer.add(buffer);

    drainBuffer(buffer);
    ASSERT_EQ(0, buffer.length());

    ZlibDecompressorImpl decompressor{stats_scope_, "test.", 4096, 100};
    decompressor.init(window_bits);

    decompressor.decompress(accumulation_buffer, buffer);
    std::string decompressed_text{buffer.toString()};

    ASSERT_EQ(compressor.checksum(), decompressor.checksum());
    ASSERT_EQ(original_text.length(), decompressed_text.length());
    EXPECT_EQ(original_text, decompressed_text);
    ASSERT_EQ(0, decompressor.decompression_error_);
  }

  static constexpr int64_t gzip_window_bits{31};
  static constexpr int64_t memory_level{8};
  static constexpr uint64_t default_input_size{796};

  Stats::IsolatedStoreImpl stats_store_{};
  Stats::Scope& stats_scope_{*stats_store_.rootScope()};
};

class ZlibDecompressorImplFailureTest : public ZlibDecompressorImplTest {
protected:
  void decompressorBadInitTestHelper(int64_t window_bits) {
    ZlibDecompressorImpl decompressor{stats_scope_, "test.", 4096, 100};
    decompressor.init(window_bits);
  }

  void uninitializedDecompressorTestHelper() {
    Buffer::OwnedImpl input_buffer;
    Buffer::OwnedImpl output_buffer;
    ZlibDecompressorImpl decompressor{stats_scope_, "test.", 4096, 100};
    TestUtility::feedBufferWithRandomCharacters(input_buffer, 100);
    decompressor.decompress(input_buffer, output_buffer);
    ASSERT_TRUE(decompressor.decompression_error_ < 0);
    ASSERT_EQ(stats_store_.counterFromString("test.zlib_stream_error").value(), 1);
  }
};

// Test different failures by passing bad initialization params or by calling decompress before
// init.
TEST_F(ZlibDecompressorImplFailureTest, DecompressorFailureTest) {
  EXPECT_DEATH(decompressorBadInitTestHelper(100), "assert failure: result >= 0");
  uninitializedDecompressorTestHelper();
}

// Exercises decompressor's checksum by calling it before init or decompress.
TEST_F(ZlibDecompressorImplTest, CallingChecksum) {
  Buffer::OwnedImpl compressor_buffer;
  Buffer::OwnedImpl decompressor_output_buffer;

  Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl compressor;
  ASSERT_EQ(0, compressor.checksum());

  compressor.init(
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
      gzip_window_bits, memory_level);
  ASSERT_EQ(0, compressor.checksum());

  TestUtility::feedBufferWithRandomCharacters(compressor_buffer, 4096);
  compressor.compress(compressor_buffer, Envoy::Compression::Compressor::State::Flush);
  ASSERT_TRUE(compressor.checksum() > 0);

  ZlibDecompressorImpl decompressor{stats_scope_, "test.", 4096, 100};
  decompressor.init(gzip_window_bits);
  EXPECT_EQ(0, decompressor.checksum());

  decompressor.decompress(compressor_buffer, decompressor_output_buffer);

  drainBuffer(compressor_buffer);
  drainBuffer(decompressor_output_buffer);

  EXPECT_EQ(compressor.checksum(), decompressor.checksum());
  ASSERT_EQ(0, decompressor.decompression_error_);
}

// Detect excessive compression ratio by compressing a long whitespace string
// into a very small chunk of data and decompressing it again.
TEST_F(ZlibDecompressorImplTest, DetectExcessiveCompressionRatio) {
  const absl::string_view ten_whitespaces = "          ";
  Buffer::OwnedImpl buffer;
  Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl compressor;
  compressor.init(
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
      gzip_window_bits, memory_level);

  for (int i = 0; i < 1000; i++) {
    buffer.add(ten_whitespaces);
  }

  compressor.compress(buffer, Envoy::Compression::Compressor::State::Finish);

  Buffer::OwnedImpl output_buffer;
  ZlibDecompressorImpl decompressor{stats_scope_, "test.", 4096, 100};
  decompressor.init(gzip_window_bits);
  decompressor.decompress(buffer, output_buffer);
  ASSERT_EQ(stats_store_.counterFromString("test.zlib_data_error").value(), 1);
}

// Exercises compression and decompression by compressing some data, decompressing it and then
// comparing compressor's input/checksum with decompressor's output/checksum.
TEST_F(ZlibDecompressorImplTest, CompressAndDecompress) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;
  Buffer::OwnedImpl empty_buffer;

  Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl compressor;
  compressor.init(
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
      gzip_window_bits, memory_level);

  std::string original_text{};
  for (uint64_t i = 0; i < 20; ++i) {
    TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
    original_text.append(buffer.toString());
    compressor.compress(buffer, Envoy::Compression::Compressor::State::Flush);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);
  }

  ASSERT_EQ(0, buffer.length());

  compressor.compress(buffer, Envoy::Compression::Compressor::State::Finish);
  ASSERT_GE(10, buffer.length());

  accumulation_buffer.add(buffer);

  drainBuffer(buffer);
  ASSERT_EQ(0, buffer.length());

  ZlibDecompressorImpl decompressor{stats_scope_, "test.", 4096, 100};
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
  ZlibDecompressorImpl decompressor{stats_scope_, "test.", 4096, 100};
  decompressor.init(gzip_window_bits);

  decompressor.decompress(accumulation_buffer, buffer);

  ASSERT_TRUE(decompressor.decompression_error_ < 0);
  ASSERT_EQ(stats_store_.counterFromString("test.zlib_data_error").value(), 17);
}

// Exercises decompression with a very small output buffer.
TEST_F(ZlibDecompressorImplTest, DecompressWithSmallOutputBuffer) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;

  Envoy::Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl compressor;
  compressor.init(
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
      gzip_window_bits, memory_level);

  std::string original_text{};
  for (uint64_t i = 0; i < 20; ++i) {
    TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
    original_text.append(buffer.toString());
    compressor.compress(buffer, Envoy::Compression::Compressor::State::Flush);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);
  }

  ASSERT_EQ(0, buffer.length());

  compressor.compress(buffer, Envoy::Compression::Compressor::State::Finish);
  ASSERT_GE(10, buffer.length());

  accumulation_buffer.add(buffer);

  drainBuffer(buffer);
  ASSERT_EQ(0, buffer.length());

  ZlibDecompressorImpl decompressor{stats_scope_, "test.", 16, 100};
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
        Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Best,
        Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Rle, 15,
        i);

    testcompressDecompressWithUncommonParams(
        Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Best,
        Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Rle, 15,
        i);

    testcompressDecompressWithUncommonParams(
        Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Speed,
        Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Huffman,
        15, i);

    testcompressDecompressWithUncommonParams(
        Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Speed,
        Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::
            Filtered,
        15, i);
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

  const uint64_t num_slices = buffer.getRawSlices().size();
  EXPECT_EQ(num_slices, 20);

  Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl compressor;
  compressor.init(
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
      gzip_window_bits, memory_level);

  compressor.compress(buffer, Envoy::Compression::Compressor::State::Flush);
  accumulation_buffer.add(buffer);

  ZlibDecompressorImpl decompressor{stats_scope_, "test.", 4096, 100};
  decompressor.init(gzip_window_bits);

  drainBuffer(buffer);
  ASSERT_EQ(0, buffer.length());

  decompressor.decompress(accumulation_buffer, buffer);
  std::string decompressed_text{buffer.toString()};

  ASSERT_EQ(compressor.checksum(), decompressor.checksum());
  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
}

class ZlibDecompressorStatsTest : public testing::Test {
protected:
  void chargeErrorStats(const int result) { decompressor_.chargeErrorStats(result); }

  Stats::IsolatedStoreImpl stats_store_{};
  Stats::Scope& stats_scope_{*stats_store_.rootScope()};
  ZlibDecompressorImpl decompressor_{stats_scope_, "test.", 4096, 100};
};

TEST_F(ZlibDecompressorStatsTest, ChargeErrorStats) {
  decompressor_.init(31);

  chargeErrorStats(Z_ERRNO);
  ASSERT_EQ(stats_store_.counterFromString("test.zlib_errno").value(), 1);
  chargeErrorStats(Z_STREAM_ERROR);
  ASSERT_EQ(stats_store_.counterFromString("test.zlib_stream_error").value(), 1);
  chargeErrorStats(Z_DATA_ERROR);
  ASSERT_EQ(stats_store_.counterFromString("test.zlib_data_error").value(), 1);
  chargeErrorStats(Z_MEM_ERROR);
  ASSERT_EQ(stats_store_.counterFromString("test.zlib_mem_error").value(), 1);
  chargeErrorStats(Z_BUF_ERROR);
  ASSERT_EQ(stats_store_.counterFromString("test.zlib_buf_error").value(), 1);
  chargeErrorStats(Z_VERSION_ERROR);
  ASSERT_EQ(stats_store_.counterFromString("test.zlib_version_error").value(), 1);
}

} // namespace Decompressor
} // namespace Gzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
