#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/compression/brotli/compressor/config.h"
#include "source/extensions/compression/brotli/decompressor/brotli_decompressor_impl.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Compressor {
namespace {

class BrotliCompressorImplTest : public testing::Test {
protected:
  void drainBuffer(Buffer::OwnedImpl& buffer) { buffer.drain(buffer.length()); }

  void verifyWithDecompressor(Envoy::Compression::Compressor::CompressorPtr compressor) {
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl accumulation_buffer;
    std::string original_text{};
    for (uint64_t i = 0; i < 10; i++) {
      TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
      original_text.append(buffer.toString());
      ASSERT_EQ(default_input_size * i, buffer.length());
      compressor->compress(buffer, Envoy::Compression::Compressor::State::Flush);
      accumulation_buffer.add(buffer);
      drainBuffer(buffer);
      ASSERT_EQ(0, buffer.length());
    }

    compressor->compress(buffer, Envoy::Compression::Compressor::State::Finish);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);

    Stats::IsolatedStoreImpl stats_store{};
    Compression::Brotli::Decompressor::BrotliDecompressorImpl decompressor{*stats_store.rootScope(),
                                                                           "test.", 4096, false};

    decompressor.decompress(accumulation_buffer, buffer);
    std::string decompressed_text{buffer.toString()};

    ASSERT_EQ(original_text.length(), decompressed_text.length());
    EXPECT_EQ(original_text, decompressed_text);
  }

  static constexpr uint32_t default_quality{11};
  static constexpr uint32_t default_window_bits{22};
  static constexpr uint32_t default_input_block_bits{22};
  static constexpr uint32_t default_input_size{796};
};

TEST_F(BrotliCompressorImplTest, CompressorDeathTest) {
  EXPECT_DEATH(
      {
        BrotliCompressorImpl compressor(1000, default_window_bits, default_input_block_bits, false,
                                        BrotliCompressorImpl::EncoderMode::Generic, 4096);
      },
      "assert failure: quality <= BROTLI_MAX_QUALITY");
  EXPECT_DEATH(
      {
        BrotliCompressorImpl compressor(default_quality, 1, default_input_block_bits, false,
                                        BrotliCompressorImpl::EncoderMode::Generic, 4096);
      },
      "assert failure: window_bits >= BROTLI_MIN_WINDOW_BITS && window_bits <= "
      "BROTLI_MAX_WINDOW_BITS");
  EXPECT_DEATH(
      {
        BrotliCompressorImpl compressor(default_quality, default_window_bits, 30, false,
                                        BrotliCompressorImpl::EncoderMode::Generic, 4096);
      },
      "assert failure: input_block_bits >= BROTLI_MIN_INPUT_BLOCK_BITS && input_block_bits <= "
      "BROTLI_MAX_INPUT_BLOCK_BITS");
}

// Compresses a fixed input and compares the output to a fixed byte pattern. Unlike the round-trip
// tests, this fails if the encoder output changes after a brotli upgrade. If the change is
// intended, check that the new bytes still decode to the plaintext, then update the pattern.
TEST_F(BrotliCompressorImplTest, CompressFixedVector) {
  const std::string plaintext{"The quick brown fox jumps over the lazy dog. The quick brown fox "
                              "jumps over the lazy dog. brotli golden vector 0123456789\n"};
  const std::string expected{"\x1b\x79\x00\x00\x1c\x09\x36\x8e\x72\xbb\x26\x4e"
                             "\xb9\x82\x4a\xdd\x96\xea\x42\x12\x8d\x51\xfc\xfd"
                             "\x85\xe3\x82\x22\xaa\x0e\x0e\x7d\x86\xbf\x1c\xe3"
                             "\x80\xf9\x06\x1c\xf6\x81\x70\x9a\x69\xf0\x90\xbd"
                             "\x24\x66\x8a\x48\xfe\x38\xc4\x5d\x4c\xe7\x45\x0f"
                             "\x2f\xa4\xdc\x8a\xfa\x1f\x84\x96\xe0\x06\xc8\xe6"
                             "\x8e\x01\x63\x88\x4e\x1c\x6d\x9b\xb4\x5c\x28\xa7"
                             "\xbb\x07",
                             86};

  Buffer::OwnedImpl buffer;
  buffer.add(plaintext);
  BrotliCompressorImpl compressor(default_quality, default_window_bits, default_input_block_bits,
                                  false, BrotliCompressorImpl::EncoderMode::Default, 4096);
  compressor.compress(buffer, Envoy::Compression::Compressor::State::Finish);
  EXPECT_EQ(expected, buffer.toString());
}

TEST_F(BrotliCompressorImplTest, CallingFinishOnly) {
  Buffer::OwnedImpl buffer;
  BrotliCompressorImpl compressor(default_quality, default_window_bits, default_input_block_bits,
                                  false, BrotliCompressorImpl::EncoderMode::Default, 4096);

  TestUtility::feedBufferWithRandomCharacters(buffer, 4096);
  compressor.compress(buffer, Envoy::Compression::Compressor::State::Finish);
}

TEST_F(BrotliCompressorImplTest, CallingFlushOnly) {
  Buffer::OwnedImpl buffer;
  BrotliCompressorImpl compressor(default_quality, default_window_bits, default_input_block_bits,
                                  false, BrotliCompressorImpl::EncoderMode::Default, 4096);

  TestUtility::feedBufferWithRandomCharacters(buffer, 4096);
  compressor.compress(buffer, Envoy::Compression::Compressor::State::Flush);
}

TEST_F(BrotliCompressorImplTest, CompressWithSmallChunkSize) {
  auto compressor = std::make_unique<BrotliCompressorImpl>(
      default_quality, default_window_bits, default_input_block_bits, false,
      BrotliCompressorImpl::EncoderMode::Default, 8);
  verifyWithDecompressor(std::move(compressor));
}

class ConfigTest : public BrotliCompressorImplTest,
                   public testing::WithParamInterface<std::string> {};

INSTANTIATE_TEST_SUITE_P(ConfigTestSuite, ConfigTest,
                         testing::Values("generic", "default", "font", "text"));

TEST_P(ConfigTest, LoadConfig) {
  absl::string_view encoder_mode = GetParam();

  std::string json{fmt::format(R"EOF({{
  "disable_literal_context_modeling": true,
  "quality": 11,
  "encoder_mode": "{}",
  "window_bits": 22,
  "input_block_bits": 24,
  "chunk_size": 4096
}})EOF",
                               encoder_mode)};
  envoy::extensions::compression::brotli::compressor::v3::Brotli brotli;
  TestUtility::loadFromJson(json, brotli);

  BrotliCompressorLibraryFactory lib_factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  Envoy::Compression::Compressor::CompressorFactoryPtr factory =
      lib_factory.createCompressorFactoryFromProto(brotli, context);
  EXPECT_EQ("brotli.", factory->statsPrefix());
  EXPECT_EQ("br", factory->contentEncoding());

  verifyWithDecompressor(factory->createCompressor());
}

} // namespace
} // namespace Compressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
