#include "extensions/filters/http/compressor/compressor_filter.h"

#include "test/mocks/compression/compressor/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {
namespace {

using testing::NiceMock;

TEST(CompressorFilterConfigTests, MakeCompressorTest) {
  const envoy::extensions::filters::http::compressor::v3::Compressor compressor_cfg;
  NiceMock<Runtime::MockLoader> runtime;
  Stats::TestUtil::TestStore stats;
  auto compressor_factory(std::make_unique<Compression::Compressor::MockCompressorFactory>());
  EXPECT_CALL(*compressor_factory, createCompressor()).Times(1);
  EXPECT_CALL(*compressor_factory, statsPrefix()).Times(1);
  EXPECT_CALL(*compressor_factory, contentEncoding()).Times(1);
  CompressorFilterConfig config(compressor_cfg, "test.compressor.", stats, runtime,
                                std::move(compressor_factory));
  Envoy::Compression::Compressor::CompressorPtr compressor = config.makeCompressor();
}

} // namespace
} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
