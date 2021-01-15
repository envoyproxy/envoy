#include "common/upstream/edf_scheduler.h"

#include "test/benchmark/main.h"

#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

using ::benchmark::State;
using Envoy::benchmark::skipExpensiveBenchmarks;

namespace Envoy {
namespace Upstream {

// NOLINTNEXTLINE(readability-identifier-naming)
template <class Edf> static void BM_PickAndAdd(State& state) {
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    // if we've been instructed to skip tests, only run once no matter the argument:
    uint32_t num_entries = skipExpensiveBenchmarks() ? 1 : state.range(0);
    Edf sched;
    std::vector<std::shared_ptr<uint32_t>> entries;
    entries.reserve(num_entries);
    for (uint32_t i = 0; i < num_entries; ++i) {
      entries.push_back(std::make_shared<uint32_t>(i));
      sched.add((i % 2) + 1, entries[i]);
    }
    for (uint32_t rounds = 0; rounds < 1000000; ++rounds) {
      auto _pick = sched.pickAndAdd([](const double&) { return 1; });
    }
  }
}

BENCHMARK_TEMPLATE(BM_PickAndAdd, EdfScheduler<uint32_t>)
    ->Ranges({{1, 10000}})
    ->Unit(::benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_PickAndAdd, TimeWheel<uint32_t>)
    ->Ranges({{1, 10000}})
    ->Unit(::benchmark::kMillisecond);

} // namespace Upstream
} // namespace Envoy
