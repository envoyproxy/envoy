#include "source/common/json/json_internal.h"
#include "source/common/json/json_sanitizer.h"
#include "source/common/protobuf/utility.h"

#include "benchmark/benchmark.h"

// NOLINT(namespace-envoy)

constexpr absl::string_view pass_through_encoding = "Now is the time for all good men";
constexpr absl::string_view escaped_encoding = "Now <is the \"time\"> for all good men";

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_ProtoEncoderNoEscape(benchmark::State& state) {
  const std::string str = std::string(pass_through_encoding);

  for (auto _ : state) { // NOLINT
    Envoy::MessageUtil::getJsonStringFromMessageOrError(Envoy::ValueUtil::stringValue(str), false,
                                                        true);
  }
}
BENCHMARK(BM_ProtoEncoderNoEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_NlohmannNoEscape(benchmark::State& state) {
  std::string buffer;

  for (auto _ : state) { // NOLINT
    Envoy::Json::sanitize(buffer, pass_through_encoding);
  }
}
BENCHMARK(BM_NlohmannNoEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_ProtoEncoderWithEscape(benchmark::State& state) {
  const std::string str = std::string(escaped_encoding);

  for (auto _ : state) { // NOLINT
    Envoy::MessageUtil::getJsonStringFromMessageOrError(Envoy::ValueUtil::stringValue(str), false,
                                                        true);
  }
}
BENCHMARK(BM_ProtoEncoderWithEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_NlohmannWithEscape(benchmark::State& state) {
  std::string buffer;

  for (auto _ : state) { // NOLINT
    Envoy::Json::sanitize(buffer, escaped_encoding);
  }
}
BENCHMARK(BM_NlohmannWithEscape);
