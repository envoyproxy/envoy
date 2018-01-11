#include <iostream>

#include "common/common/utility.h"

#include "testing/base/public/benchmark.h"

static const char TextToTrim[] = "\t  the quick brown fox jumps over the lazy dog\n\r\n";
static size_t TextToTrimLength = sizeof(TextToTrim) - 1;

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

static void BM_RTrimStringView(benchmark::State& state) {
  int accum = 0;
  int iters = 0;
  for (auto _ : state) {
    absl::string_view text(TextToTrim, TextToTrimLength);
    text = Envoy::StringUtil::rightTrim(text);
    accum += TextToTrimLength - text.size();
    ++iters;
  }
  printResults(accum, iters);
}
BENCHMARK(BM_RTrimStringView);

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;
  benchmark::RunSpecifiedBenchmarks();
}
