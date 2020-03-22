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

static void compressWith(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel level,
                         Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy strategy,
                         int64_t window_bits, uint64_t memory_level) {
  Stats::IsolatedStoreImpl stats;
  testing::NiceMock<Runtime::MockLoader> runtime;
  envoy::extensions::filters::http::compressor::v3::Compressor compressor;
  CompressorFilterConfigSharedPtr config = std::make_shared<MockCompressorFilterConfig>(
      compressor, "test.", stats, runtime, "gzip", level, strategy, window_bits, memory_level);
  Buffer::OwnedImpl data;
  TestUtility::feedBufferWithRandomCharacters(data, 122880);

  ON_CALL(runtime.snapshot_, featureEnabled("test.filter_enabled", 100))
      .WillByDefault(Return(true));

  auto filter = std::make_unique<CompressorFilter>(config);
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
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
Compress/0          32358062 ns     32338955 ns           22
Compress/0          31778249 ns     31761636 ns           22
Compress/0          32731151 ns     32559273 ns           22
Compress/0          32162121 ns     32094000 ns           22
Compress/0          31838115 ns     31827091 ns           22
Compress/0_mean     32173540 ns     32116191 ns            5
Compress/0_median   32162121 ns     32094000 ns            5
Compress/0_stddev     391750 ns       337537 ns            5
Compress/1          25973147 ns     25959880 ns           25
Compress/1          25694425 ns     25647480 ns           25
Compress/1          25471416 ns     25428160 ns           25
Compress/1          26939314 ns     26772800 ns           25
Compress/1          26003532 ns     25902160 ns           25
Compress/1_mean     26016367 ns     25942096 ns            5
Compress/1_median   25973147 ns     25902160 ns            5
Compress/1_stddev     560018 ns       510615 ns            5
Compress/2          25198150 ns     25057345 ns           29
Compress/2          24647784 ns     24569862 ns           29
Compress/2          24051937 ns     24041379 ns           29
Compress/2          24163591 ns     24124310 ns           29
Compress/2          24264160 ns     24227103 ns           29
Compress/2_mean     24465124 ns     24404000 ns            5
Compress/2_median   24264160 ns     24227103 ns            5
Compress/2_stddev     467098 ns       416948 ns            5
Compress/3          33208504 ns     33158286 ns           21
Compress/3          33045705 ns     32999524 ns           21
Compress/3          32968974 ns     32942952 ns           21
Compress/3          33151030 ns     33117810 ns           21
Compress/3          33145734 ns     33080429 ns           21
Compress/3_mean     33103989 ns     33059800 ns            5
Compress/3_median   33145734 ns     33080429 ns            5
Compress/3_stddev      95531 ns        87716 ns            5
Compress/4          28887816 ns     28849560 ns           25
Compress/4          28655442 ns     28631800 ns           25
Compress/4          28357271 ns     28328480 ns           25
Compress/4          28638322 ns     28609480 ns           25
Compress/4          28472287 ns     28426120 ns           25
Compress/4_mean     28602227 ns     28569088 ns            5
Compress/4_median   28638322 ns     28609480 ns            5
Compress/4_stddev     201608 ns       201594 ns            5
Compress/5          34508363 ns     34294333 ns           21
Compress/5          35917562 ns     35903190 ns           21
Compress/5          36917294 ns     36859429 ns           21
Compress/5          36721353 ns     36688286 ns           21
Compress/5          36689713 ns     36636571 ns           21
Compress/5_mean     36150857 ns     36076362 ns            5
Compress/5_median   36689713 ns     36636571 ns            5
Compress/5_stddev     994418 ns      1061496 ns            5
Compress/6          33070916 ns     32995700 ns           20
Compress/6          32491178 ns     32477400 ns           20
Compress/6          32957946 ns     32878450 ns           20
Compress/6          34205187 ns     34135600 ns           20
Compress/6          33364057 ns     33343950 ns           20
Compress/6_mean     33217857 ns     33166220 ns            5
Compress/6_median   33070916 ns     32995700 ns            5
Compress/6_stddev     635099 ns       624029 ns            5
Compress/7          28629389 ns     28615250 ns           24
Compress/7          29117726 ns     29049417 ns           24
Compress/7          28957688 ns     28901708 ns           24
Compress/7          28865586 ns     28819458 ns           24
Compress/7          28954294 ns     28911250 ns           24
Compress/7_mean     28904937 ns     28859417 ns            5
Compress/7_median   28954294 ns     28901708 ns            5
Compress/7_stddev     178867 ns       159522 ns            5
Compress/8          34586577 ns     34565300 ns           20
Compress/8          35307657 ns     35196450 ns           20
Compress/8          35069861 ns     35007600 ns           20
Compress/8          34858071 ns     34830350 ns           20
Compress/8          34895321 ns     34816050 ns           20
Compress/8_mean     34943497 ns     34883150 ns            5
Compress/8_median   34895321 ns     34830350 ns            5
Compress/8_stddev     267204 ns       235565 ns            5
*/
// SPELLCHECKER(on)

static std::vector<
    std::tuple<Envoy::Compressor::ZlibCompressorImpl::CompressionLevel,
               Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy, int64_t, uint64_t>>
    compression_params = {
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

static void Compress(benchmark::State& state) {
  const auto idx = state.range(0);
  const auto& params = compression_params[idx];

  for (auto _ : state) {
    compressWith(std::get<0>(params), std::get<1>(params), std::get<2>(params),
                 std::get<3>(params));
  }
}
BENCHMARK(Compress)->DenseRange(0, 8, 1);

} // namespace Compressors
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

BENCHMARK_MAIN();
