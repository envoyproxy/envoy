#include "common/upstream/edf_scheduler.h"

#include "test/benchmark/main.h"

#include "gtest/gtest.h"
#include "benchmark/benchmark.h"

using ::benchmark::State;
using Envoy::benchmark::skipExpensiveBenchmarks;

namespace Envoy {
namespace Upstream {

class EdfSpeedTest {
public:
  EdfSpeedTest(State& state) : state_(state) {}
  State& state_;
};

static void heap(State& state) {
  for (auto _ : state) {
    // Envoy::Upstream::EdsSpeedTest speed_test(state, state.range(0));
    // if we've been instructed to skip tests, only run once no matter the argument:
    uint32_t num_entries = skipExpensiveBenchmarks() ? 1 : state.range(0);

    EdfScheduler<uint32_t> sched;
    std::vector<std::shared_ptr<uint32_t>> entries;
    entries.reserve(num_entries);
    for (uint32_t i = 0; i < num_entries; ++i) {
      entries.push_back(std::make_shared<uint32_t>(i));
      sched.add(1, entries[i]);
    }
    for (uint32_t rounds = 0; rounds < 128; ++rounds) {
      for (uint32_t i = 0; i < num_entries; ++i) {
        auto _peek = sched.peekAgain([](const double&) { return 1; });
        auto _pick = sched.pickAndAdd([](const double&) { return 1; });
      }
    }
  }
}


static void wheel(State& state) {
  for (auto _ : state) {
    // Envoy::Upstream::EdsSpeedTest speed_test(state, state.range(0));
    // if we've been instructed to skip tests, only run once no matter the argument:
    uint32_t num_entries = skipExpensiveBenchmarks() ? 1 : state.range(0);

    TimeWheel<uint32_t> sched;
    std::vector<std::shared_ptr<uint32_t>> entries;
    entries.reserve(num_entries);
    for (uint32_t i = 0; i < num_entries; ++i) {
      entries.push_back(std::make_shared<uint32_t>(i));
      sched.add(1, entries[i]);
    }
    for (uint32_t rounds = 0; rounds < 128; ++rounds) {
      for (uint32_t i = 0; i < num_entries; ++i) {
        auto _peek = sched.peekAgain([](const double&) { return 1; });
        auto _pick = sched.pickAndAdd([](const double&) { return 1; });
      }
    }
  }
}

BENCHMARK(heap)->Ranges({{1, 10000}})->Unit(::benchmark::kMillisecond);
BENCHMARK(wheel)->Ranges({{1, 10000}})->Unit(::benchmark::kMillisecond);

} // namespace Upstream
} // namespace Envoy
