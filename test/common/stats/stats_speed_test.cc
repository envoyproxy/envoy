//#include <algorithm>
//#include <chrono>

//#include "envoy/stats/stats_macros.h"

//#include "common/config/well_known_names.h"
#include "common/stats/stats_impl.h"

//#include "test/test_common/utility.h"
#include "testing/base/public/benchmark.h"


namespace Envoy {
namespace Stats {

void createManyStats(int numStats) {
  IsolatedStoreImpl store;
  ScopePtr scope1 = store.createScope("scope1.");
  for (int i = 0; i < numStats; ++i) {
    scope1->counter(fmt::format("c{}", i));
  }
}

} // namespace Stats
} // namespace Envoy


static void BM_100_Stats(benchmark::State& state) {
  for (auto _ : state) {
    Envoy::Stats::createManyStats(100);
  }
}
BENCHMARK(BM_100_Stats);

static void BM_1000_Stats(benchmark::State& state) {
  for (auto _ : state) {
    Envoy::Stats::createManyStats(1000);
  }
}
BENCHMARK(BM_1000_Stats);

static void BM_10k_Stats(benchmark::State& state) {
  for (auto _ : state) {
    Envoy::Stats::createManyStats(10000);
  }
}
BENCHMARK(BM_10k_Stats);

static void BM_100k_Stats(benchmark::State& state) {
  for (auto _ : state) {
    Envoy::Stats::createManyStats(100000);
  }
}
BENCHMARK(BM_100k_Stats);

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
