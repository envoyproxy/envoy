#include "extensions/filters/http/compressor/gzip/config.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {
namespace Gzip {

class GzipTest : public testing::Test {
protected:
  void SetUp() override { setUpGzip("{}"); }

  // GzipTest Helpers
  void setUpGzip(std::string&& json) {
    envoy::extensions::filters::http::compressor::gzip::v3::Gzip gzip;
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

  void expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy strategy,
      absl::string_view strategy_name,
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel level, absl::string_view level_name) {
    setUpGzip(fmt::format(
        R"EOF({{"compression_strategy": "{}", "compression_level": "{}", "memory_level": 6, "window_bits": 27}})EOF",
        strategy_name, level_name));
    EXPECT_EQ(strategy, compressionStrategy());
    EXPECT_EQ(level, compressionLevel());
    EXPECT_EQ(6, memoryLevel());
    EXPECT_EQ(27, windowBits());
  }

  std::unique_ptr<GzipCompressorFactory> factory_;
};

// Default config values.
TEST_F(GzipTest, DefaultConfigValues) {
  EXPECT_EQ(5, memoryLevel());
  EXPECT_EQ(28, windowBits());
  EXPECT_EQ(Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
            compressionStrategy());
  EXPECT_EQ(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard, compressionLevel());
}

TEST_F(GzipTest, AvailableCombinationCompressionStrategyAndLevelConfig) {
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Filtered, "FILTERED",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Best, "BEST");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Huffman, "HUFFMAN",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Best, "BEST");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Rle, "RLE",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Speed, "SPEED");
  expectValidCompressionStrategyAndLevel(
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, "DEFAULT",
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard, "DEFAULT");
}

} // namespace Gzip
} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
