#include "external_lib.h"

#include "benchmark/benchmark.h"

static void BM_ExternalLibrary(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(External::externalValue());
  }
}
BENCHMARK(BM_ExternalLibrary);
