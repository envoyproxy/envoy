// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.
//
// Exercises both paths of StatsSharedImpl::decRefCount():
//   * Fast path  (ref_count_ > 1): single-thread, multi-thread same stat,
//                                  multi-thread distinct stats.
//   * Slow path  (ref_count_ == 1): single-thread.
//
// Fast-path benchmarks pre-seed ref_count_ so every dec in the timed loop
// stays on the CAS path. Slow-path benchmark pre-allocates one counter per
// iteration with ref_count_=1, so each timed dec frees its counter.

#include <limits>
#include <optional>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/stats/allocator.h"
#include "source/common/stats/symbol_table.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"

namespace Envoy {
namespace Stats {
namespace {

class DecRefCountFixture {
public:
  explicit DecRefCountFixture(size_t num_counters) : alloc_(symbol_table_), pool_(symbol_table_) {
    counters_.reserve(num_counters);
    for (size_t i = 0; i < num_counters; ++i) {
      counters_.push_back(
          alloc_.makeCounter(pool_.add(absl::StrCat("benchmark.counter.", i)), StatName(), {}));
    }
  }

  Counter& counter(size_t i) { return *counters_[i]; }
  CounterSharedPtr& counterPtr(size_t i) { return counters_[i]; }
  size_t size() const { return counters_.size(); }

private:
  SymbolTable symbol_table_;
  Allocator alloc_;
  StatNamePool pool_;
  std::vector<CounterSharedPtr> counters_;
};

// Single thread, single stat, ref_count_ > 1
void bmDecRefCountFastPathSingleThread(benchmark::State& state) {
  DecRefCountFixture fixture(1);
  Counter& counter = fixture.counter(0);
  RELEASE_ASSERT(state.max_iterations < std::numeric_limits<uint32_t>::max() - 1,
                 "ref_count_ will overflow");
  for (uint32_t i = 0; i < state.max_iterations; i++) {
    counter.incRefCount();
  }
  for (auto _ : state) { // NOLINT
    const bool freed = counter.decRefCount();
    benchmark::DoNotOptimize(freed);
  }
}
BENCHMARK(bmDecRefCountFastPathSingleThread);

std::optional<DecRefCountFixture> multi_thread_fixture;
void multiThreadInitSameStat(const benchmark::State&) { multi_thread_fixture.emplace(1); }
void multiThreadInitDistinctStat(const benchmark::State& state) {
  multi_thread_fixture.emplace(state.threads());
}
void multiThreadDestroy(const benchmark::State&) { multi_thread_fixture.reset(); }

// Multiple threads, same stat, ref_count_ > 1
void bmDecRefCountFastPathMultiThreadSameStat(benchmark::State& state) {
  Counter& counter = multi_thread_fixture->counter(0);
  RELEASE_ASSERT(static_cast<uint64_t>(state.max_iterations) * state.threads() <
                     std::numeric_limits<uint32_t>::max() - 1,
                 "ref_count_ will overflow across all threads");
  for (uint32_t i = 0; i < state.max_iterations; i++) {
    counter.incRefCount();
  }
  for (auto _ : state) { // NOLINT
    const bool freed = counter.decRefCount();
    benchmark::DoNotOptimize(freed);
  }
}
BENCHMARK(bmDecRefCountFastPathMultiThreadSameStat)
    ->ThreadRange(1, 16)
    ->UseRealTime()
    ->Setup(multiThreadInitSameStat)
    ->Teardown(multiThreadDestroy);

// Multiple threads, different stats, ref_count_ > 1
void bmDecRefCountFastPathMultiThreadDistinctStat(benchmark::State& state) {
  Counter& counter = multi_thread_fixture->counter(state.thread_index());
  RELEASE_ASSERT(state.max_iterations < std::numeric_limits<uint32_t>::max() - 1,
                 "ref_count_ will overflow on a per-thread counter");
  for (uint32_t i = 0; i < state.max_iterations; i++) {
    counter.incRefCount();
  }
  for (auto _ : state) { // NOLINT
    const bool freed = counter.decRefCount();
    benchmark::DoNotOptimize(freed);
  }
}
BENCHMARK(bmDecRefCountFastPathMultiThreadDistinctStat)
    ->ThreadRange(1, 16)
    ->UseRealTime()
    ->Setup(multiThreadInitDistinctStat)
    ->Teardown(multiThreadDestroy);

// Single thread, multiple stats, ref_count_ == 1
void bmDecRefCountSlowPathSingleThread(benchmark::State& state) {
  DecRefCountFixture fixture(state.max_iterations);
  size_t idx = 0;
  for (auto _ : state) { // NOLINT
    fixture.counterPtr(idx++).reset();
  }
}
BENCHMARK(bmDecRefCountSlowPathSingleThread)->Iterations(1 << 18);

} // namespace
} // namespace Stats
} // namespace Envoy
