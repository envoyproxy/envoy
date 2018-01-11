// Note: this should be run with --compilation_mode=opt.

#include <iostream>

#include "common/common/utility.h"

#include "testing/base/public/benchmark.h"

static const char TextToTrim[] = "\t  the quick brown fox jumps over the lazy dog\n\r\n";
static size_t TextToTrimLength = sizeof(TextToTrim) - 1;

static const char AlreadyTrimmed[] = "the quick brown fox jumps over the lazy dog";
static size_t AlreadyTrimmedLength = sizeof(AlreadyTrimmed) - 1;

// NOLINT(namespace-envoy)

static void printResults(int accum, int iters) {
  std::cout << "avg trimmed=" << static_cast<float>(accum) / iters << " of " << iters << " iters."
            << std::endl;
}

static void BM_RTrimString(benchmark::State& state) {
  int accum = 0;
  int iters = 0;
  for (auto _ : state) {
    std::string text(TextToTrim, TextToTrimLength);
    Envoy::StringUtil::rtrim(text);
    accum += TextToTrimLength - text.size();
    ++iters;
  }
  printResults(accum, iters);
}
BENCHMARK(BM_RTrimString);

static void BM_RTrimStringAlreadyTrimmed(benchmark::State& state) {
  int accum = 0;
  int iters = 0;
  for (auto _ : state) {
    std::string text(AlreadyTrimmed, AlreadyTrimmedLength);
    Envoy::StringUtil::rtrim(text);
    accum += AlreadyTrimmedLength - text.size();
    ++iters;
  }
  printResults(accum, iters);
}
BENCHMARK(BM_RTrimStringAlreadyTrimmed);

// TODO(jmarantz): delete this and call gsagula's version from
// https://github.com/gsagula/envoy/blob/02ba40dd236645b89aec7689a81c9ff89f71dfc2/source/common/common/utility.cc#L222
// once it's merged.
static absl::string_view rightTrim(absl::string_view source) {
  source.remove_suffix(source.size() - source.find_last_not_of(" \t\f\v\n\r") - 1);
  return source;
}

static void BM_RTrimStringView(benchmark::State& state) {
  int accum = 0;
  int iters = 0;
  for (auto _ : state) {
    absl::string_view text(TextToTrim, TextToTrimLength);
    text = rightTrim(text);
    accum += TextToTrimLength - text.size();
    ++iters;
  }
  printResults(accum, iters);
}
BENCHMARK(BM_RTrimStringView);

static void BM_RTrimStringViewAlreadyTrimmed(benchmark::State& state) {
  int accum = 0;
  int iters = 0;
  for (auto _ : state) {
    absl::string_view text(AlreadyTrimmed, AlreadyTrimmedLength);
    text = rightTrim(text);
    accum += AlreadyTrimmedLength - text.size();
    ++iters;
  }
  printResults(accum, iters);
}
BENCHMARK(BM_RTrimStringViewAlreadyTrimmed);

static void BM_RTrimStringViewAlreadyTrimmedAndMakeString(benchmark::State& state) {
  int accum = 0;
  int iters = 0;
  for (auto _ : state) {
    absl::string_view text(AlreadyTrimmed, AlreadyTrimmedLength);
    std::string string_copy = std::string(rightTrim(text));
    accum += AlreadyTrimmedLength - string_copy.size();
    ++iters;
  }
  printResults(accum, iters);
}
BENCHMARK(BM_RTrimStringViewAlreadyTrimmedAndMakeString);

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;
  benchmark::RunSpecifiedBenchmarks();
}
