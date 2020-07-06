#include "envoy/extensions/filters/http/compressor/v3/compressor.pb.h"

#include "extensions/compression/gzip/compressor/zlib_compressor_impl.h"
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
      Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel level,
      Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy strategy,
      int64_t window_bits, uint64_t memory_level)
      : CompressorFilterConfig(compressor, stats_prefix + compressor_name + ".", scope, runtime,
                               compressor_name),
        level_(level), strategy_(strategy), window_bits_(window_bits), memory_level_(memory_level) {
  }

  Envoy::Compression::Compressor::CompressorPtr makeCompressor() override {
    auto compressor = std::make_unique<Compression::Gzip::Compressor::ZlibCompressorImpl>();
    compressor->init(level_, strategy_, window_bits_, memory_level_);
    return compressor;
  }

  const Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel level_;
  const Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy strategy_;
  const int64_t window_bits_;
  const uint64_t memory_level_;
};

using CompressionParams =
    std::tuple<Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel,
               Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy, int64_t,
               uint64_t>;

static constexpr uint64_t TestDataSize = 122880;

Buffer::OwnedImpl generateTestData() {
  Buffer::OwnedImpl data;
  TestUtility::feedBufferWithRandomCharacters(data, TestDataSize);
  return data;
}

const Buffer::OwnedImpl& testData() {
  CONSTRUCT_ON_FIRST_USE(Buffer::OwnedImpl, generateTestData());
}

static std::vector<Buffer::OwnedImpl> generateChunks(const uint64_t chunk_count,
                                                     const uint64_t chunk_size) {
  std::vector<Buffer::OwnedImpl> vec;
  vec.reserve(chunk_count);

  const auto& test_data = testData();
  uint64_t added = 0;

  for (uint64_t i = 0; i < chunk_count; ++i) {
    Buffer::OwnedImpl chunk;
    std::unique_ptr<char[]> data(new char[chunk_size]);

    test_data.copyOut(added, chunk_size, data.get());
    chunk.add(absl::string_view(data.get(), chunk_size));
    vec.push_back(std::move(chunk));

    added += chunk_size;
  }

  return vec;
}

struct Result {
  uint64_t total_uncompressed_bytes = 0;
  uint64_t total_compressed_bytes = 0;
};

static Result compressWith(std::vector<Buffer::OwnedImpl>&& chunks, CompressionParams params,
                           NiceMock<Http::MockStreamDecoderFilterCallbacks>& decoder_callbacks,
                           benchmark::State& state) {
  auto start = std::chrono::high_resolution_clock::now();
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
  Result res;
  for (auto& data : chunks) {
    res.total_uncompressed_bytes += data.length();

    if (idx == (chunks.size() - 1)) {
      filter->encodeData(data, true);
    } else {
      filter->encodeData(data, false);
    }

    res.total_compressed_bytes += data.length();
    ++idx;
  }

  EXPECT_EQ(res.total_uncompressed_bytes,
            stats.counterFromString("test.gzip.total_uncompressed_bytes").value());
  EXPECT_EQ(res.total_compressed_bytes,
            stats.counterFromString("test.gzip.total_compressed_bytes").value());

  EXPECT_EQ(1U, stats.counterFromString("test.gzip.compressed").value());
  auto end = std::chrono::high_resolution_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
  state.SetIterationTime(elapsed.count());

  return res;
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
compressFull/0/manual_time              14.1 ms         14.3 ms           48
compressFull/1/manual_time              7.06 ms         7.22 ms          104
compressFull/2/manual_time              5.17 ms         5.33 ms          123
compressFull/3/manual_time              15.4 ms         15.5 ms           45
compressFull/4/manual_time              10.1 ms         10.3 ms           69
compressFull/5/manual_time              15.8 ms         16.0 ms           40
compressFull/6/manual_time              15.3 ms         15.5 ms           42
compressFull/7/manual_time              9.91 ms         10.1 ms           71
compressFull/8/manual_time              15.8 ms         16.0 ms           45
compressChunks16384/0/manual_time       13.4 ms         13.5 ms           52
compressChunks16384/1/manual_time       6.33 ms         6.48 ms          111
compressChunks16384/2/manual_time       5.09 ms         5.27 ms          147
compressChunks16384/3/manual_time       15.1 ms         15.3 ms           46
compressChunks16384/4/manual_time       9.61 ms         9.78 ms           71
compressChunks16384/5/manual_time       14.5 ms         14.6 ms           47
compressChunks16384/6/manual_time       14.0 ms         14.1 ms           48
compressChunks16384/7/manual_time       9.20 ms         9.36 ms           76
compressChunks16384/8/manual_time       14.5 ms         14.6 ms           48
compressChunks8192/0/manual_time        14.3 ms         14.5 ms           50
compressChunks8192/1/manual_time        6.80 ms         6.96 ms          100
compressChunks8192/2/manual_time        5.21 ms         5.36 ms          135
compressChunks8192/3/manual_time        14.9 ms         15.0 ms           47
compressChunks8192/4/manual_time        9.71 ms         9.87 ms           68
compressChunks8192/5/manual_time        15.9 ms         16.1 ms           45
....
*/
// SPELLCHECKER(on)

static std::vector<CompressionParams> compression_params = {
    // Speed + Standard + Small Window + Low mem level
    {Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Speed,
     Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 9, 1},

    // Speed + Standard + Med window + Med mem level
    {Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Speed,
     Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 12, 5},

    // Speed + Standard + Big window + High mem level
    {Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Speed,
     Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 15, 9},

    // Standard + Standard + Small window + Low mem level
    {Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
     Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 9, 1},

    // Standard + Standard + Med window + Med mem level
    {Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
     Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 12, 5},

    // Standard + Standard + High window + High mem level
    {Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
     Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 15, 9},

    // Best + Standard + Small window + Low mem level
    {Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Best,
     Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 9, 1},

    // Best + Standard + Med window + Med mem level
    {Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Best,
     Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 12, 5},

    // Best + Standard + High window + High mem level
    {Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Best,
     Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 15, 9}};

static void compressFull(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = compression_params[idx];

  for (auto _ : state) {
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(1, 122880);
    compressWith(std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressFull)->DenseRange(0, 8, 1)->UseManualTime()->Unit(benchmark::kMillisecond);

static void compressChunks16384(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = compression_params[idx];

  for (auto _ : state) {
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(7, 16384);
    compressWith(std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks16384)->DenseRange(0, 8, 1)->UseManualTime()->Unit(benchmark::kMillisecond);

static void compressChunks8192(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = compression_params[idx];

  for (auto _ : state) {
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(15, 8192);
    compressWith(std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks8192)->DenseRange(0, 8, 1)->UseManualTime()->Unit(benchmark::kMillisecond);

static void compressChunks4096(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = compression_params[idx];

  for (auto _ : state) {
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(30, 4096);
    compressWith(std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks4096)->DenseRange(0, 8, 1)->UseManualTime()->Unit(benchmark::kMillisecond);

static void compressChunks1024(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = compression_params[idx];

  for (auto _ : state) {
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(120, 1024);
    compressWith(std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks1024)->DenseRange(0, 8, 1)->UseManualTime()->Unit(benchmark::kMillisecond);

} // namespace Compressors
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
