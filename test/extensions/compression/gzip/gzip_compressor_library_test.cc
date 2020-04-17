#include "extensions/compression/gzip/compressor/config.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Gzip {
namespace Compressor {

class GzipTest : public testing::Test {
protected:
  void SetUp() override { setUpGzip("{}"); }

  // GzipTest Helpers
  void setUpGzip(std::string&& json) {
    envoy::extensions::compression::gzip::compressor::v3::Gzip gzip;
    TestUtility::loadFromJson(json, gzip);
    factory_ = std::make_unique<GzipCompressorFactory>(gzip);
  }

  Envoy::Compressor::ZlibCompressorImpl::CompressionLevel compressionLevel() const {
    return factory_->compression_level_;
  }

  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy compressionStrategy() const {
    return factory_->compression_strategy_;
  }

  int32_t memoryLevel() const { return factory_->memory_level_; }
  int32_t windowBits() const { return factory_->window_bits_; }
  int32_t chunkSize() const { return factory_->chunk_size_; }

  void expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy strategy,
      absl::string_view strategy_name,
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel level, absl::string_view level_name) {
    setUpGzip(fmt::format(
        R"EOF({{"compression_strategy": "{}", "compression_level": "{}", "memory_level": 6, "window_bits": 27, "chunk_size": 10000}})EOF",
        strategy_name, level_name));
    EXPECT_EQ(strategy, compressionStrategy());
    EXPECT_EQ(level, compressionLevel());
    EXPECT_EQ(6, memoryLevel());
    EXPECT_EQ(27, windowBits());
    EXPECT_EQ(10000, chunkSize());
  }

  std::unique_ptr<GzipCompressorFactory> factory_;
};

// Default config values.
TEST_F(GzipTest, DefaultConfigValues) {
  EXPECT_EQ(5, memoryLevel());
  EXPECT_EQ(28, windowBits());
  EXPECT_EQ(4096, chunkSize());
  EXPECT_EQ(Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
            compressionStrategy());
  EXPECT_EQ(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard, compressionLevel());
}

TEST_F(GzipTest, AvailableCombinationCompressionStrategyAndLevelConfig) {
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Filtered, "FILTERED",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Best, "BEST_COMPRESSION");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Huffman, "HUFFMAN_ONLY",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Best, "BEST_COMPRESSION");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Rle, "RLE",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Speed, "BEST_SPEED");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, "DEFAULT_STRATEGY",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard, "DEFAULT_COMPRESSION");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Fixed, "FIXED",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level1, "COMPRESSION_LEVEL_1");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Fixed, "FIXED",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level2, "COMPRESSION_LEVEL_2");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Fixed, "FIXED",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level3, "COMPRESSION_LEVEL_3");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Fixed, "FIXED",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level4, "COMPRESSION_LEVEL_4");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Fixed, "FIXED",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level5, "COMPRESSION_LEVEL_5");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Fixed, "FIXED",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level6, "COMPRESSION_LEVEL_6");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Fixed, "FIXED",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level7, "COMPRESSION_LEVEL_7");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Fixed, "FIXED",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level8, "COMPRESSION_LEVEL_8");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Fixed, "FIXED",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Level9, "COMPRESSION_LEVEL_9");
}

} // namespace Compressor
} // namespace Gzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
