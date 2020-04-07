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
      const std::string& compressor_name,
      Envoy::Compressor::ZlibCompressorImpl::CompressionLevel level,
      Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy strategy, int64_t window_bits,
      uint64_t memory_level)
      : CompressorFilterConfig(compressor, stats_prefix + compressor_name + ".", scope, runtime,
                               compressor_name),
        level_(level), strategy_(strategy), window_bits_(window_bits), memory_level_(memory_level) {
  }

  std::unique_ptr<Compressor::Compressor> makeCompressor() override {
    auto compressor = std::make_unique<Compressor::ZlibCompressorImpl>();
    compressor->init(level_, strategy_, window_bits_, memory_level_);
    return compressor;
  }

  const Envoy::Compressor::ZlibCompressorImpl::CompressionLevel level_;
  const Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy strategy_;
  const int64_t window_bits_;
  const uint64_t memory_level_;
};

using CompressionParams =
    std::tuple<Envoy::Compressor::ZlibCompressorImpl::CompressionLevel,
               Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy, int64_t, uint64_t>;

static std::vector<Buffer::OwnedImpl> generateChunks(const uint64_t chunk_count,
                                                     const uint64_t chunk_size) {
  std::vector<Buffer::OwnedImpl> vec;
  vec.reserve(chunk_count);

  Buffer::OwnedImpl data;
  TestUtility::feedBufferWithRandomCharacters(data, chunk_size);

  for (uint64_t i = 0; i < chunk_count; ++i) {
    Buffer::OwnedImpl chunk;
    chunk.add(data);
    vec.push_back(std::move(chunk));
  }

  return vec;
}

static void compressWith(std::vector<Buffer::OwnedImpl>&& chunks, CompressionParams params,
                         NiceMock<Http::MockStreamDecoderFilterCallbacks>& decoder_callbacks) {
  Stats::IsolatedStoreImpl stats;
  testing::NiceMock<Runtime::MockLoader> runtime;
  envoy::extensions::filters::http::compressor::v3::Compressor compressor;

  const auto level = std::get<0>(params);
  const auto strategy = std::get<1>(params);
  const auto window_bits = std::get<2>(params);
  const auto memory_level = std::get<3>(params);
  CompressorFilterConfigSharedPtr config = std::make_shared<MockCompressorFilterConfig>(
      compressor, "test.", stats, runtime, "gzip", level, strategy, window_bits, memory_level);

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

  uint64_t idx = 0;
  uint64_t total_uncompressed_bytes = 0;
  uint64_t total_compressed_bytes = 0;
  for (auto& data : chunks) {
    total_uncompressed_bytes += data.length();

    if (idx == (chunks.size() - 1)) {
      filter->encodeData(data, true);
    } else {
      filter->encodeData(data, false);
    }

    total_compressed_bytes += data.length();
    ++idx;
  }

  EXPECT_EQ(total_uncompressed_bytes,
            stats.counterFromString("test.gzip.total_uncompressed_bytes").value());
  EXPECT_EQ(total_compressed_bytes,
            stats.counterFromString("test.gzip.total_compressed_bytes").value());
  EXPECT_EQ(1U, stats.counterFromString("test.gzip.compressed").value());
}

// SPELLCHECKER(off)
/*
Running ./bazel-bin/test/extensions/filters/http/common/compressor/compressor_filter_speed_test
Run on (8 X 2300 MHz CPU s)
CPU Caches:
L1 Data 32K (x4)
L1 Instruction 32K (x4)
L2 Unified 262K (x4)
L3 Unified 6291K (x1)
Load Average: 1.82, 1.72, 1.74
***WARNING*** Library was built as DEBUG. Timings may be affected.
------------------------------------------------------------
Benchmark                  Time             CPU   Iterations
------------------------------------------------------------
....

CompressChunks8192/2           2971499 ns      2967258 ns          248
CompressChunks8192/2           3015538 ns      3008694 ns          248
CompressChunks8192/2           2919954 ns      2907698 ns          248
CompressChunks8192/2           2838894 ns      2831851 ns          248
CompressChunks8192/2           2867619 ns      2865883 ns          248
CompressChunks8192/2_mean      2922701 ns      2916277 ns            5
CompressChunks8192/2_median    2919954 ns      2907698 ns            5
CompressChunks8192/2_stddev      72569 ns        72251 ns            5
....
*/
// SPELLCHECKER(on)

static std::vector<CompressionParams> compression_params = {
    // Speed + Standard + Small Window + Low mem level
    {Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Speed,
     Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 9, 1},

    // Speed + Standard + Med window + Med mem level
    {Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Speed,
     Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 12, 5},

    // Speed + Standard + Big window + High mem level
    {Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Speed,
     Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 15, 9},

    // Standard + Standard + Small window + Low mem level
    {Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
     Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 9, 1},

    // Standard + Standard + Med window + Med mem level
    {Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
     Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 12, 5},

    // Standard + Standard + High window + High mem level
    {Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
     Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 15, 9},

    // Best + Standard + Small window + Low mem level
    {Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Best,
     Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 9, 1},

    // Best + Standard + Med window + Med mem level
    {Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Best,
     Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 12, 5},

    // Best + Standard + High window + High mem level
    {Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Best,
     Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 15, 9}};

static void compressChunks16384(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = compression_params[idx];

  for (auto _ : state) {
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(7, 16384);
    compressWith(std::move(chunks), params, decoder_callbacks);
  }
}
BENCHMARK(compressChunks16384)->DenseRange(0, 8, 1);

static void compressChunks8192(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = compression_params[idx];

  for (auto _ : state) {
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(15, 8192);
    compressWith(std::move(chunks), params, decoder_callbacks);
  }
}
BENCHMARK(compressChunks8192)->DenseRange(0, 8, 1);

static void compressChunks4096(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = compression_params[idx];

  for (auto _ : state) {
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(30, 4096);
    compressWith(std::move(chunks), params, decoder_callbacks);
  }
}
BENCHMARK(compressChunks4096)->DenseRange(0, 8, 1);

static void compressChunks1024(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = compression_params[idx];

  for (auto _ : state) {
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(120, 1024);
    compressWith(std::move(chunks), params, decoder_callbacks);
  }
}
BENCHMARK(compressChunks1024)->DenseRange(0, 8, 1);

static void compressFull(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = compression_params[idx];

  for (auto _ : state) {
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(1, 122880);
    compressWith(std::move(chunks), params, decoder_callbacks);
  }
}
BENCHMARK(compressFull)->DenseRange(0, 8, 1);

} // namespace Compressors
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

BENCHMARK_MAIN();
