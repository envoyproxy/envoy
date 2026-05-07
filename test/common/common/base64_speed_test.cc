// Note: this should be run with --compilation_mode=opt, and would benefit from
// a quiescent system with disabled cstate power management.

#include "source/common/common/base64.h"

#include "base64_legacy.h"
#include "benchmark/benchmark.h"

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AbslBase64Encode(benchmark::State& state) {
  const std::string input(state.range(0), 'x');
  for (auto _ : state) {
    std::string res = Envoy::Base64::encode(input);
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_AbslBase64Encode)->Range(8, 1 << 20);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_LegacyBase64Encode(benchmark::State& state) {
  const std::string input(state.range(0), 'x');
  for (auto _ : state) {
    std::string res = Envoy::Base64Legacy::encode(input);
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_LegacyBase64Encode)->Range(8, 1 << 20);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AbslBase64Decode(benchmark::State& state) {
  const std::string input(state.range(0), 'x');
  const std::string encoded = Envoy::Base64::encode(input);

  for (auto _ : state) {
    std::string res = Envoy::Base64::decode(encoded);
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}
BENCHMARK(BM_AbslBase64Decode)->Range(8, 1 << 20);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_LegacyBase64Decode(benchmark::State& state) {
  const std::string input(state.range(0), 'x');
  const std::string encoded = Envoy::Base64::encode(input);

  for (auto _ : state) {
    std::string res = Envoy::Base64Legacy::decode(encoded);
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}
BENCHMARK(BM_LegacyBase64Decode)->Range(8, 1 << 20);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AbslBase64UrlEncode(benchmark::State& state) {
  const std::string input(state.range(0), 'x');
  for (auto _ : state) {
    std::string res = Envoy::Base64Url::encode(input.data(), input.size());
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_AbslBase64UrlEncode)->Range(8, 1 << 20);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_LegacyBase64UrlEncode(benchmark::State& state) {
  const std::string input(state.range(0), 'x');
  for (auto _ : state) {
    std::string res = Envoy::Base64UrlLegacy::encode(input.data(), input.size());
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_LegacyBase64UrlEncode)->Range(8, 1 << 20);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AbslBase64UrlDecode(benchmark::State& state) {
  const std::string input(state.range(0), 'x');
  const std::string encoded = Envoy::Base64Url::encode(input.data(), input.size());

  for (auto _ : state) {
    std::string res = Envoy::Base64Url::decode(encoded);
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}
BENCHMARK(BM_AbslBase64UrlDecode)->Range(8, 1 << 20);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_LegacyBase64UrlDecode(benchmark::State& state) {
  const std::string input(state.range(0), 'x');
  const std::string encoded = Envoy::Base64Url::encode(input.data(), input.size());

  for (auto _ : state) {
    std::string res = Envoy::Base64UrlLegacy::decode(encoded);
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}
BENCHMARK(BM_LegacyBase64UrlDecode)->Range(8, 1 << 20);
