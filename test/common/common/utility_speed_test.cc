// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "common/common/utility.h"

#include "absl/strings/string_view.h"
#include "common/common/assert.h"
#include "testing/base/public/benchmark.h"

static const char TextToTrim[] = "\t  the quick brown fox jumps over the lazy dog\n\r\n";
static size_t TextToTrimLength = sizeof(TextToTrim) - 1;

static const char AlreadyTrimmed[] = "the quick brown fox jumps over the lazy dog";
static size_t AlreadyTrimmedLength = sizeof(AlreadyTrimmed) - 1;

static const char CacheControl[] = "private, max-age=300, no-transform";
static size_t CacheControlLength = sizeof(CacheControl) - 1;

// NOLINT(namespace-envoy)

static void BM_RTrimStringView(benchmark::State& state) {
  int accum = 0;
  for (auto _ : state) {
    absl::string_view text(TextToTrim, TextToTrimLength);
    text = Envoy::StringUtil::rtrim(text);
    accum += TextToTrimLength - text.size();
  }
  benchmark::DoNotOptimize(accum);
}
BENCHMARK(BM_RTrimStringView);

static void BM_RTrimStringViewAlreadyTrimmed(benchmark::State& state) {
  int accum = 0;
  for (auto _ : state) {
    absl::string_view text(AlreadyTrimmed, AlreadyTrimmedLength);
    text = Envoy::StringUtil::rtrim(text);
    accum += AlreadyTrimmedLength - text.size();
  }
  benchmark::DoNotOptimize(accum);
}
BENCHMARK(BM_RTrimStringViewAlreadyTrimmed);

static void BM_RTrimStringViewAlreadyTrimmedAndMakeString(benchmark::State& state) {
  int accum = 0;
  for (auto _ : state) {
    absl::string_view text(AlreadyTrimmed, AlreadyTrimmedLength);
    std::string string_copy = std::string(Envoy::StringUtil::rtrim(text));
    accum += AlreadyTrimmedLength - string_copy.size();
  }
  benchmark::DoNotOptimize(accum);
}
BENCHMARK(BM_RTrimStringViewAlreadyTrimmedAndMakeString);

static void BM_FindToken(benchmark::State& state) {
  const absl::string_view cache_control(CacheControl, CacheControlLength);
  for (auto _ : state) {
    RELEASE_ASSERT(Envoy::StringUtil::findToken(cache_control, ",", "no-transform"));
  }
}
BENCHMARK(BM_FindToken);

static void BM_FindTokenValue(benchmark::State& state) {
  const absl::string_view cache_control(CacheControl, CacheControlLength);
  absl::string_view max_age;
  for (auto _ : state) {
    for (absl::string_view token : Envoy::StringUtil::splitToken(cache_control, ",")) {
      auto name_value = Envoy::StringUtil::splitToken(token, "=");
      if ((name_value.size() == 2) && (Envoy::StringUtil::trim(name_value[0]) == "max-age")) {
        max_age = Envoy::StringUtil::trim(name_value[1]);
      }
    }
    RELEASE_ASSERT(max_age == "300");
  }
}
BENCHMARK(BM_FindTokenValue);

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
