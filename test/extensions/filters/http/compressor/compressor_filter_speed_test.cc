#include "envoy/compression/compressor/factory.h"
#include "envoy/extensions/filters/http/compressor/v3/compressor.pb.h"

#include "source/extensions/compression/brotli/compressor/brotli_compressor_impl.h"
#include "source/extensions/compression/brotli/compressor/config.h"
#include "source/extensions/compression/gzip/compressor/config.h"
#include "source/extensions/compression/gzip/compressor/zlib_compressor_impl.h"
#include "source/extensions/compression/zstd/compressor/zstd_compressor_impl.h"
#include "source/extensions/filters/http/compressor/compressor_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {
class MockGzipCompressorFactory : public Envoy::Compression::Compressor::CompressorFactory {
public:
  MockGzipCompressorFactory(
      Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel level,
      Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy strategy,
      int64_t window_bits, uint64_t memory_level)
      : level_(level), strategy_(strategy), window_bits_(window_bits), memory_level_(memory_level) {
  }

  Envoy::Compression::Compressor::CompressorPtr createCompressor() override {
    auto compressor = std::make_unique<Compression::Gzip::Compressor::ZlibCompressorImpl>(
        Compression::Gzip::Compressor::DefaultChunkSize);
    compressor->init(level_, strategy_, window_bits_, memory_level_);
    return compressor;
  }

  const std::string& statsPrefix() const override { CONSTRUCT_ON_FIRST_USE(std::string, "gzip."); }
  const std::string& contentEncoding() const override {
    return Http::CustomHeaders::get().ContentEncodingValues.Gzip;
  }

private:
  const Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel level_;
  const Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy strategy_;
  const int64_t window_bits_;
  const uint64_t memory_level_;
};

class MockZstdCompressorFactory : public Envoy::Compression::Compressor::CompressorFactory {
public:
  MockZstdCompressorFactory(uint32_t level, uint32_t strategy)
      : level_(level), strategy_(strategy) {}

  Envoy::Compression::Compressor::CompressorPtr createCompressor() override {
    return std::make_unique<Compression::Zstd::Compressor::ZstdCompressorImpl>(
        level_, enable_checksum_, strategy_, cdict_manager_, chunk_size_);
  }

  const std::string& statsPrefix() const override { CONSTRUCT_ON_FIRST_USE(std::string, "zstd."); }
  const std::string& contentEncoding() const override {
    return Http::CustomHeaders::get().ContentEncodingValues.Zstd;
  }

private:
  const uint32_t level_;
  const uint32_t strategy_;
  const bool enable_checksum_{};
  Compression::Zstd::Compressor::ZstdCDictManagerPtr cdict_manager_{nullptr};
  const uint64_t chunk_size_{4096};
};

class MockBrotliCompressorFactory : public Envoy::Compression::Compressor::CompressorFactory {
public:
  explicit MockBrotliCompressorFactory(uint32_t quality) : quality_(quality) {}

  Envoy::Compression::Compressor::CompressorPtr createCompressor() override {
    return std::make_unique<Compression::Brotli::Compressor::BrotliCompressorImpl>(
        quality_, Compression::Brotli::Compressor::DefaultWindowBits,
        Compression::Brotli::Compressor::DefaultInputBlockBits, disable_literal_context_modeling_,
        mode_, Compression::Brotli::Compressor::DefaultChunkSize);
  }

  const std::string& statsPrefix() const override {
    CONSTRUCT_ON_FIRST_USE(std::string, "brotli.");
  }
  const std::string& contentEncoding() const override {
    return Http::CustomHeaders::get().ContentEncodingValues.Brotli;
  }

private:
  const Compression::Brotli::Compressor::BrotliCompressorImpl::EncoderMode mode_{
      Compression::Brotli::Compressor::BrotliCompressorImpl::EncoderMode::Generic};
  const uint32_t quality_;
  const bool disable_literal_context_modeling_{false};
};

struct CompressionParams {
  int64_t level;
  uint64_t strategy;
  int64_t window_bits;
  uint64_t memory_level;
};

CompressorFilterConfigSharedPtr makeGzipConfig(Stats::IsolatedStoreImpl& stats,
                                               testing::NiceMock<Runtime::MockLoader>& runtime,
                                               const CompressionParams& params) {

  envoy::extensions::filters::http::compressor::v3::Compressor compressor;

  const auto level =
      static_cast<Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel>(
          params.level);
  const auto strategy =
      static_cast<Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy>(
          params.strategy);
  const auto window_bits = params.window_bits;
  const auto memory_level = params.memory_level;
  Envoy::Compression::Compressor::CompressorFactoryPtr compressor_factory =
      std::make_unique<MockGzipCompressorFactory>(level, strategy, window_bits, memory_level);
  CompressorFilterConfigSharedPtr config = std::make_shared<CompressorFilterConfig>(
      compressor, "test.", *stats.rootScope(), runtime, std::move(compressor_factory));

  return config;
}

CompressorFilterConfigSharedPtr makeZstdConfig(Stats::IsolatedStoreImpl& stats,
                                               testing::NiceMock<Runtime::MockLoader>& runtime,
                                               const CompressionParams& params) {

  envoy::extensions::filters::http::compressor::v3::Compressor compressor;

  const auto level = params.level;
  const auto strategy = params.strategy;
  Envoy::Compression::Compressor::CompressorFactoryPtr compressor_factory =
      std::make_unique<MockZstdCompressorFactory>(level, strategy);
  CompressorFilterConfigSharedPtr config = std::make_shared<CompressorFilterConfig>(
      compressor, "test.", *stats.rootScope(), runtime, std::move(compressor_factory));

  return config;
}

CompressorFilterConfigSharedPtr makeBrotliConfig(Stats::IsolatedStoreImpl& stats,
                                                 testing::NiceMock<Runtime::MockLoader>& runtime,
                                                 const CompressionParams& params) {

  envoy::extensions::filters::http::compressor::v3::Compressor compressor;

  const auto quality = params.level;
  Envoy::Compression::Compressor::CompressorFactoryPtr compressor_factory =
      std::make_unique<MockBrotliCompressorFactory>(quality);
  CompressorFilterConfigSharedPtr config = std::make_shared<CompressorFilterConfig>(
      compressor, "test.", *stats.rootScope(), runtime, std::move(compressor_factory));

  return config;
}

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

enum class CompressorLibs { Brotli, Gzip, Zstd };

// Ignore the gmock overhead due to it has been measured with flame graphs to be pretty low.
// And you should build with `--compilation_mode=opt --cxxopt=-g --cxxopt=-ggdb3` to get code
// optimizations.
static Result compressWith(enum CompressorLibs lib, std::vector<Buffer::OwnedImpl>&& chunks,
                           CompressionParams params,
                           NiceMock<Http::MockStreamDecoderFilterCallbacks>& decoder_callbacks,
                           benchmark::State& state) {
  auto start = std::chrono::high_resolution_clock::now();
  Stats::IsolatedStoreImpl stats;
  testing::NiceMock<Runtime::MockLoader> runtime;
  CompressorFilterConfigSharedPtr config;
  std::string compressor = "";
  std::string encoding = "";
  if (lib == CompressorLibs::Brotli) {
    config = makeBrotliConfig(stats, runtime, params);
    encoding = "br";
    compressor = "brotli";
  } else if (lib == CompressorLibs::Gzip) {
    config = makeGzipConfig(stats, runtime, params);
    encoding = compressor = "gzip";
  } else if (lib == CompressorLibs::Zstd) {
    config = makeZstdConfig(stats, runtime, params);
    encoding = compressor = "zstd";
  }

  ON_CALL(runtime.snapshot_, featureEnabled("test.filter_enabled", 100))
      .WillByDefault(Return(true));

  auto filter = std::make_unique<CompressorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks);

  Http::TestRequestHeaderMapImpl headers = {
      {":method", "get"}, {"accept-encoding", encoding}, {"content-encoding", encoding}};
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
            stats
                .counterFromString(
                    absl::StrCat("test.compressor..", compressor, ".total_uncompressed_bytes"))
                .value());
  EXPECT_EQ(res.total_compressed_bytes,
            stats
                .counterFromString(
                    absl::StrCat("test.compressor..", compressor, ".total_compressed_bytes"))
                .value());

  EXPECT_EQ(1U,
            stats.counterFromString(absl::StrCat("test.compressor..", compressor, ".compressed"))
                .value());
  auto end = std::chrono::high_resolution_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
  state.SetIterationTime(elapsed.count());

  return res;
}

// SPELLCHECKER(off)
/*
You can sepcify "--benchmark_filter=Gzip" to select compressor to benchmark.
Running ./bazel-bin/test/extensions/filters/http/compressor/compressor_filter_speed_test
Run on (112 X 1784.9 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x56)
  L1 Instruction 32 KiB (x56)
  L2 Unified 1280 KiB (x56)
  L3 Unified 43008 KiB (x2)
Load Average: 0.32, 0.52, 0.70
***WARNING*** Library was built as DEBUG. Timings may be affected.
------------------------------------------------------------------------------------
Benchmark                                          Time             CPU   Iterations
------------------------------------------------------------------------------------
compressFullWithGzip/0/manual_time              15.6 ms         16.2 ms           45
compressFullWithGzip/1/manual_time              7.46 ms         8.01 ms           94
compressFullWithGzip/2/manual_time              5.67 ms         6.22 ms          124
compressFullWithGzip/3/manual_time              16.7 ms         17.3 ms           42
compressFullWithGzip/4/manual_time              10.9 ms         11.4 ms           64
compressFullWithGzip/5/manual_time              17.9 ms         18.5 ms           39
compressFullWithGzip/6/manual_time              16.7 ms         17.2 ms           42
compressFullWithGzip/7/manual_time              10.9 ms         11.4 ms           64
compressFullWithGzip/8/manual_time              18.0 ms         18.5 ms           39
compressChunks16384WithGzip/0/manual_time       14.6 ms         15.2 ms           47
compressChunks16384WithGzip/1/manual_time       7.08 ms         7.65 ms           99
compressChunks16384WithGzip/2/manual_time       5.49 ms         6.06 ms          127
compressChunks16384WithGzip/3/manual_time       15.7 ms         16.3 ms           45
compressChunks16384WithGzip/4/manual_time       10.3 ms         10.9 ms           68
compressChunks16384WithGzip/5/manual_time       16.9 ms         17.4 ms           41
compressChunks16384WithGzip/6/manual_time       15.7 ms         16.3 ms           44
compressChunks16384WithGzip/7/manual_time       10.3 ms         10.9 ms           68
compressChunks16384WithGzip/8/manual_time       16.8 ms         17.4 ms           41
compressChunks8192WithGzip/0/manual_time        15.7 ms         16.3 ms           45
compressChunks8192WithGzip/1/manual_time        7.56 ms         8.16 ms           93
compressChunks8192WithGzip/2/manual_time        5.90 ms         6.51 ms          118
compressChunks8192WithGzip/3/manual_time        16.9 ms         17.4 ms           42
compressChunks8192WithGzip/4/manual_time        11.0 ms         11.6 ms           64
compressChunks8192WithGzip/5/manual_time        18.2 ms         18.8 ms           38
compressChunks8192WithGzip/6/manual_time        16.9 ms         17.5 ms           42
....
*/
// SPELLCHECKER(on)

static constexpr CompressionParams gzip_compression_params[] = {
    // Speed + Standard + Small Window + Low mem level
    {Z_BEST_SPEED, Z_DEFAULT_STRATEGY, 9, 1},

    // Speed + Standard + Med window + Med mem level
    {Z_BEST_SPEED, Z_DEFAULT_STRATEGY, 12, 5},

    // Speed + Standard + Big window + High mem level
    {Z_BEST_SPEED, Z_DEFAULT_STRATEGY, 15, 9},

    // Standard + Standard + Small window + Low mem level
    {Z_DEFAULT_COMPRESSION, Z_DEFAULT_STRATEGY, 9, 1},

    // Standard + Standard + Med window + Med mem level
    {Z_DEFAULT_COMPRESSION, Z_DEFAULT_STRATEGY, 12, 5},

    // Standard + Standard + High window + High mem level
    {Z_DEFAULT_COMPRESSION, Z_DEFAULT_STRATEGY, 15, 9},

    // Best + Standard + Small window + Low mem level
    {Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY, 9, 1},

    // Best + Standard + Med window + Med mem level
    {Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY, 12, 5},

    // Best + Standard + High window + High mem level
    {Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY, 15, 9}};

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressFullWithGzip(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = gzip_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(1, 122880);
    compressWith(CompressorLibs::Gzip, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressFullWithGzip)
    ->DenseRange(0, 8, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressChunks16384WithGzip(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = gzip_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(7, 16384);
    compressWith(CompressorLibs::Gzip, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks16384WithGzip)
    ->DenseRange(0, 8, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressChunks8192WithGzip(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = gzip_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(15, 8192);
    compressWith(CompressorLibs::Gzip, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks8192WithGzip)
    ->DenseRange(0, 8, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressChunks4096WithGzip(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = gzip_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(30, 4096);
    compressWith(CompressorLibs::Gzip, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks4096WithGzip)
    ->DenseRange(0, 8, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressChunks1024WithGzip(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = gzip_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(120, 1024);
    compressWith(CompressorLibs::Gzip, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks1024WithGzip)
    ->DenseRange(0, 8, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

static constexpr CompressionParams zstd_compression_params[] = {
    // level1 + default
    {1, 0, 0, 0},

    // level2 + default
    {2, 0, 0, 0},

    // level3 + default
    {3, 0, 0, 0},

    // level4 + default
    {4, 0, 0, 0},

    // level5 + default
    {5, 0, 0, 0},

    // level6 + default
    {6, 0, 0, 0},

    // level7 + default
    {7, 0, 0, 0},

    // level8 + default
    {8, 0, 0, 0},

    // level9 + default
    {9, 0, 0, 0},

    // level10 + default
    {10, 0, 0, 0},

    // level11 + default
    {11, 0, 0, 0},

    // level12 + default
    {12, 0, 0, 0},

    // level13 + default
    {13, 0, 0, 0},

    // level14 + default
    {14, 0, 0, 0},

    // level15 + default
    {15, 0, 0, 0},

    // level16 + default
    {16, 0, 0, 0},

    // level17 + default
    {17, 0, 0, 0},

    // level18 + default
    {18, 0, 0, 0},

    // level19 + default
    {19, 0, 0, 0},

    // level20 + default
    {20, 0, 0, 0},

    // level21 + default
    {21, 0, 0, 0},

    // level22 + default
    {22, 0, 0, 0}};

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressFullWithZstd(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = zstd_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(1, 122880);
    compressWith(CompressorLibs::Zstd, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressFullWithZstd)
    ->DenseRange(0, 21, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressChunks16384WithZstd(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = zstd_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(7, 16384);
    compressWith(CompressorLibs::Zstd, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks16384WithZstd)
    ->DenseRange(0, 21, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressChunks8192WithZstd(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = zstd_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(15, 8192);
    compressWith(CompressorLibs::Zstd, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks8192WithZstd)
    ->DenseRange(0, 21, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressChunks4096WithZstd(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = zstd_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(30, 4096);
    compressWith(CompressorLibs::Zstd, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks4096WithZstd)
    ->DenseRange(0, 21, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressChunks1024WithZstd(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = zstd_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(120, 1024);
    compressWith(CompressorLibs::Zstd, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks1024WithZstd)
    ->DenseRange(0, 21, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

static constexpr CompressionParams brotli_compression_params[] = {
    // level1 + default
    {1, 0, 0, 0},

    // level2 + default
    {2, 0, 0, 0},

    // level3 + default
    {3, 0, 0, 0},

    // level4 + default
    {4, 0, 0, 0},

    // level5 + default
    {5, 0, 0, 0},

    // level6 + default
    {6, 0, 0, 0},

    // level7 + default
    {7, 0, 0, 0},

    // level8 + default
    {8, 0, 0, 0},

    // level9 + default
    {9, 0, 0, 0},

    // level10 + default
    {10, 0, 0, 0},

    // level11 + default
    {11, 0, 0, 0}};

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressFullWithBrotli(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = brotli_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(1, 122880);
    compressWith(CompressorLibs::Brotli, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressFullWithBrotli)
    ->DenseRange(0, 10, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressChunks16384WithBrotli(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = brotli_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(7, 16384);
    compressWith(CompressorLibs::Brotli, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks16384WithBrotli)
    ->DenseRange(0, 10, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressChunks8192WithBrotli(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = brotli_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(15, 8192);
    compressWith(CompressorLibs::Brotli, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks8192WithBrotli)
    ->DenseRange(0, 10, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressChunks4096WithBrotli(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = brotli_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(30, 4096);
    compressWith(CompressorLibs::Brotli, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks4096WithBrotli)
    ->DenseRange(0, 10, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void compressChunks1024WithBrotli(benchmark::State& state) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  const auto idx = state.range(0);
  const auto& params = brotli_compression_params[idx];

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::vector<Buffer::OwnedImpl> chunks = generateChunks(120, 1024);
    compressWith(CompressorLibs::Brotli, std::move(chunks), params, decoder_callbacks, state);
  }
}
BENCHMARK(compressChunks1024WithBrotli)
    ->DenseRange(0, 10, 1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
