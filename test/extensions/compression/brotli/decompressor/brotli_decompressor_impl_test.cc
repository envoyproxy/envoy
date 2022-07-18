#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/compression/brotli/compressor/brotli_compressor_impl.h"
#include "source/extensions/compression/brotli/decompressor/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Decompressor {
namespace {

class BrotliDecompressorImplTest : public testing::Test {
protected:
  void drainBuffer(Buffer::OwnedImpl& buffer) { buffer.drain(buffer.length()); }

  static constexpr uint32_t default_quality{2};
  static constexpr uint32_t default_window_bits{22};
  static constexpr uint32_t default_input_block_bits{22};
  static constexpr uint32_t default_input_size{796};
};

// Detect excessive compression ratio by compressing a long whitespace string
// into a very small chunk of data and decompressing it again.
TEST_F(BrotliDecompressorImplTest, DetectExcessiveCompressionRatio) {
  const absl::string_view ten_whitespaces = "          ";
  Brotli::Compressor::BrotliCompressorImpl compressor{
      default_quality,
      default_window_bits,
      default_input_block_bits,
      false,
      Brotli::Compressor::BrotliCompressorImpl::EncoderMode::Default,
      4096};
  Buffer::OwnedImpl buffer;

  for (int i = 0; i < 1000; i++) {
    buffer.add(ten_whitespaces);
  }

  compressor.compress(buffer, Envoy::Compression::Compressor::State::Finish);

  Buffer::OwnedImpl output_buffer;
  Stats::IsolatedStoreImpl stats_store{};
  BrotliDecompressorImpl decompressor{stats_store, "test.", 16, false};
  decompressor.decompress(buffer, output_buffer);
  EXPECT_EQ(1, stats_store.counterFromString("test.brotli_error").value());
}

// Exercises compression and decompression by compressing some data, decompressing it and then
// comparing compressor's input/checksum with decompressor's output/checksum.
TEST_F(BrotliDecompressorImplTest, CompressAndDecompress) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;

  Brotli::Compressor::BrotliCompressorImpl compressor{
      default_quality,
      default_window_bits,
      default_input_block_bits,
      false,
      Brotli::Compressor::BrotliCompressorImpl::EncoderMode::Default,
      4096};

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

  std::string json{R"EOF({
  "disable_ring_buffer_reallocation": false,
  "chunk_size": 4096
})EOF"};
  envoy::extensions::compression::brotli::decompressor::v3::Brotli brotli;
  TestUtility::loadFromJson(json, brotli);

  BrotliDecompressorLibraryFactory lib_factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  Envoy::Compression::Decompressor::DecompressorFactoryPtr factory =
      lib_factory.createDecompressorFactoryFromProto(brotli, context);
  EXPECT_EQ("brotli.", factory->statsPrefix());
  EXPECT_EQ("br", factory->contentEncoding());

  Envoy::Compression::Decompressor::DecompressorPtr decompressor =
      factory->createDecompressor("test.");
  decompressor->decompress(accumulation_buffer, buffer);
  std::string decompressed_text{buffer.toString()};
  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
}

// Exercises decompression with a very small output buffer.
TEST_F(BrotliDecompressorImplTest, DecompressWithSmallOutputBuffer) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;

  Brotli::Compressor::BrotliCompressorImpl compressor{
      default_quality,
      default_window_bits,
      default_input_block_bits,
      false,
      Brotli::Compressor::BrotliCompressorImpl::EncoderMode::Default,
      4096};

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

  Stats::IsolatedStoreImpl stats_store{};
  BrotliDecompressorImpl decompressor{stats_store, "test.", 16, false};

  decompressor.decompress(accumulation_buffer, buffer);
  std::string decompressed_text{buffer.toString()};

  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
  EXPECT_EQ(0, stats_store.counterFromString("test.brotli_error").value());
}

TEST_F(BrotliDecompressorImplTest, WrongInput) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl output_buffer;
  const char zeros[20]{};

  Buffer::BufferFragmentImpl* frag = new Buffer::BufferFragmentImpl(
      zeros, 20, [](const void*, size_t, const Buffer::BufferFragmentImpl* frag) { delete frag; });
  buffer.addBufferFragment(*frag);
  Stats::IsolatedStoreImpl stats_store{};
  BrotliDecompressorImpl decompressor{stats_store, "test.", 16, false};
  decompressor.decompress(buffer, output_buffer);
  EXPECT_EQ(1, stats_store.counterFromString("test.brotli_error").value());
}

TEST_F(BrotliDecompressorImplTest, CompressDecompressOfMultipleSlices) {
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

  Brotli::Compressor::BrotliCompressorImpl compressor{
      default_quality,
      default_window_bits,
      default_input_block_bits,
      false,
      Brotli::Compressor::BrotliCompressorImpl::EncoderMode::Default,
      4096};

  compressor.compress(buffer, Envoy::Compression::Compressor::State::Flush);
  accumulation_buffer.add(buffer);

  Stats::IsolatedStoreImpl stats_store{};
  BrotliDecompressorImpl decompressor{stats_store, "test.", 16, false};

  drainBuffer(buffer);
  ASSERT_EQ(0, buffer.length());

  decompressor.decompress(accumulation_buffer, buffer);
  std::string decompressed_text{buffer.toString()};

  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
  EXPECT_EQ(0, stats_store.counterFromString("test.brotli_error").value());
}

class UncommonParamsTest : public BrotliDecompressorImplTest,
                           public testing::WithParamInterface<std::tuple<bool, bool>> {
protected:
  void testcompressDecompressWithUncommonParams(
      const uint32_t quality, const uint32_t window_bits, const uint32_t input_block_bits,
      const bool disable_literal_context_modeling,
      const Compression::Brotli::Compressor::BrotliCompressorImpl::EncoderMode encoder_mode,
      const bool disable_ring_buffer_reallocation) {
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl accumulation_buffer;

    Compression::Brotli::Compressor::BrotliCompressorImpl compressor{
        quality,      window_bits, input_block_bits, disable_literal_context_modeling,
        encoder_mode, 4096};

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

    Stats::IsolatedStoreImpl stats_store{};
    BrotliDecompressorImpl decompressor{stats_store, "test.", 4096,
                                        disable_ring_buffer_reallocation};

    decompressor.decompress(accumulation_buffer, buffer);
    std::string decompressed_text{buffer.toString()};

    ASSERT_EQ(original_text.length(), decompressed_text.length());
    EXPECT_EQ(original_text, decompressed_text);
    EXPECT_EQ(0, stats_store.counterFromString("test.brotli_error").value());
  }
};

INSTANTIATE_TEST_SUITE_P(UncommonParamsTestSuite, UncommonParamsTest,
                         testing::Values(std::make_tuple(false, false),
                                         std::make_tuple(false, true), std::make_tuple(true, false),
                                         std::make_tuple(true, true)));

// Exercises decompression with other supported brotli initialization params.
TEST_P(UncommonParamsTest, Validate) {
  const bool disable_literal_context_modeling = std::get<0>(GetParam());
  const bool disable_ring_buffer_reallocation = std::get<1>(GetParam());

  // Test with different memory levels.
  for (uint32_t i = 1; i < 8; ++i) {
    testcompressDecompressWithUncommonParams(
        i - 1,                    // quality
        default_window_bits,      // window_bits
        default_input_block_bits, // input_block_bits
        disable_literal_context_modeling,
        Brotli::Compressor::BrotliCompressorImpl::EncoderMode::Font,
        disable_ring_buffer_reallocation);

    testcompressDecompressWithUncommonParams(
        default_quality,     // quality
        default_window_bits, // window_bits
        i + 15,              // input_block_bits
        disable_literal_context_modeling,
        Brotli::Compressor::BrotliCompressorImpl::EncoderMode::Text,
        disable_ring_buffer_reallocation);

    testcompressDecompressWithUncommonParams(
        default_quality,          // quality
        i + 10,                   // window_bits
        default_input_block_bits, // input_block_bits
        disable_literal_context_modeling,
        Brotli::Compressor::BrotliCompressorImpl::EncoderMode::Generic,
        disable_ring_buffer_reallocation);
  }
}

} // namespace
} // namespace Decompressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
