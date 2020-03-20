#include "envoy/extensions/filters/http/compressor/v3/compressor.pb.h"

#include "common/compressor/zlib_compressor_impl.h"

#include "extensions/filters/http/common/compressor/compressor.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Compressors {

class MockCompressorFilterConfig : public CompressorFilterConfig {
public:
  MockCompressorFilterConfig(
      const envoy::extensions::filters::http::compressor::v3::Compressor& compressor,
      const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
      const std::string& compressor_name)
      : CompressorFilterConfig(compressor, stats_prefix + compressor_name + ".", scope, runtime,
                               compressor_name) {}

  std::unique_ptr<Compressor::Compressor> makeCompressor() override {
    auto compressor = std::make_unique<Compressor::ZlibCompressorImpl>();
    compressor->init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Speed,
                     Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 15, 9);
    return compressor;
  }
};

// Measure basic compression.
static void FilterCompress(benchmark::State& state) {
  for (auto _ : state) {
    CompressorFilterConfigSharedPtr config;
    Stats::IsolatedStoreImpl stats;
    testing::NiceMock<Runtime::MockLoader> runtime;
    envoy::extensions::filters::http::compressor::v3::Compressor compressor;
    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    Buffer::OwnedImpl data;

    TestUtility::feedBufferWithRandomCharacters(data, 122880);

    config.reset(new MockCompressorFilterConfig(compressor, "test.", stats, runtime, "gzip"));

    ON_CALL(runtime.snapshot_, featureEnabled("test.filter_enabled", 100))
        .WillByDefault(Return(true));

    auto filter = std::make_unique<CompressorFilter>(config);
    filter->setDecoderFilterCallbacks(decoder_callbacks);

    Http::TestRequestHeaderMapImpl headers = {{":method", "get"}, {"accept-encoding", "gzip"}};
    filter->decodeHeaders(headers, false);

    Http::TestResponseHeaderMapImpl response_headers = {
        {":method", "get"},
        {"content-length", "122880"},
        {"content-type", "application/json;charset=utf-8"}};
    filter->encodeHeaders(response_headers, false);
    filter->encodeData(data, true);

    EXPECT_EQ(122880, stats.counter("test.gzip.total_uncompressed_bytes").value());
    EXPECT_EQ(data.length(), stats.counter("test.gzip.total_compressed_bytes").value());
    EXPECT_EQ(1U, stats.counter("test.gzip.compressed").value());
  }
}
BENCHMARK(FilterCompress);

} // namespace Compressors
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

// Boilerplate main().
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
