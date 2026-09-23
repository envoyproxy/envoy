// NOLINT(namespace-envoy)
#include "test/benchmark/main.h"

#include "benchmark/benchmark.h"

static void BM_ExternalBenchmark(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(Envoy::benchmark::skipExpensiveBenchmarks());
  }
}

BENCHMARK(BM_ExternalBenchmark);
