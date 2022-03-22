#include "source/common/json/json_sanitizer.h"
#include "source/common/protobuf/utility.h"

#include "benchmark/benchmark.h"

// NOLINT(namespace-envoy)

constexpr absl::string_view pass_through_encoding = "Now is the time for all good men";
constexpr absl::string_view escaped_encoding = "Now <is the \"time\"> for all good men";

const Envoy::Json::JsonSanitizer& static_sanitizer() {
  CONSTRUCT_ON_FIRST_USE(Envoy::Json::JsonSanitizer);
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_ProtoEncoderNoEscape(benchmark::State& state) {
  const std::string str = std::string(pass_through_encoding);

  for (auto _ : state) { // NOLINT
    Envoy::MessageUtil::getJsonStringFromMessageOrDie(Envoy::ValueUtil::stringValue(str), false,
                                                      true);
  }
}
BENCHMARK(BM_ProtoEncoderNoEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_JsonSanitizerNoEscape(benchmark::State& state) {
  Envoy::Json::JsonSanitizer sanitizer;
  std::string buffer;

  for (auto _ : state) { // NOLINT
    sanitizer.sanitize(buffer, pass_through_encoding);
  }
}
BENCHMARK(BM_JsonSanitizerNoEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_JsonSanitizerNoEscapeWithClear(benchmark::State& state) {
  Envoy::Json::JsonSanitizer sanitizer;
  std::string buffer("Now is the time for all good men..."); // >22 chars

  for (auto _ : state) { // NOLINT
    buffer.clear();
    sanitizer.sanitize(buffer, pass_through_encoding);
  }
}
BENCHMARK(BM_JsonSanitizerNoEscapeWithClear);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StaticJsonSanitizerNoEscape(benchmark::State& state) {
  std::string buffer;

  for (auto _ : state) { // NOLINT
    static_sanitizer().sanitize(buffer, pass_through_encoding);
  }
}
BENCHMARK(BM_StaticJsonSanitizerNoEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_ProtoEncoderWithEscape(benchmark::State& state) {
  const std::string str = std::string(escaped_encoding);

  for (auto _ : state) { // NOLINT
    Envoy::MessageUtil::getJsonStringFromMessageOrDie(Envoy::ValueUtil::stringValue(str), false,
                                                      true);
  }
}
BENCHMARK(BM_ProtoEncoderWithEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_JsonSanitizerWithEscape(benchmark::State& state) {
  Envoy::Json::JsonSanitizer sanitizer;
  std::string buffer;

  for (auto _ : state) { // NOLINT
    sanitizer.sanitize(buffer, escaped_encoding);
  }
}
BENCHMARK(BM_JsonSanitizerWithEscape);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StaticJsonSanitizerWithEscape(benchmark::State& state) {
  std::string buffer;

  for (auto _ : state) { // NOLINT
    static_sanitizer().sanitize(buffer, escaped_encoding);
  }
}
BENCHMARK(BM_StaticJsonSanitizerWithEscape);
