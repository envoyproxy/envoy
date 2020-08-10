// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.
//
// NOLINT(namespace-envoy)
//
// Running bazel-bin/test/common/stats/recent_lookups_speed_test
// Run on (12 X 4500 MHz CPU s)
// CPU Caches:
//   L1 Data 32K (x6)
//   L1 Instruction 32K (x6)
//   L2 Unified 1024K (x6)
//   L3 Unified 8448K (x1)
// Load Average: 1.32, 7.40, 10.21
// ***WARNING*** CPU scaling is enabled, the benchmark real time measurements may be noisy and will
// incur extra overhead.
// -----------------------------------------------------------------
// Benchmark                       Time             CPU   Iterations
// -----------------------------------------------------------------
// BM_LookupsMixed             87068 ns        87068 ns         6955
// BM_LookupsNoEvictions       45662 ns        45662 ns        15329
// BM_LookupsAllEvictions      83015 ns        83015 ns         8435

#include "common/common/random_generator.h"
#include "common/runtime/runtime_impl.h"
#include "common/stats/recent_lookups.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"

class RecentLookupsSpeedTest {
public:
  RecentLookupsSpeedTest(uint64_t lookup_variants, uint64_t capacity) {
    recent_lookups_.setCapacity(capacity);
    Envoy::Random::RandomGeneratorImpl random;
    lookups_.reserve(lookup_variants);
    for (size_t i = 0; i < lookup_variants; ++i) {
      lookups_.push_back(absl::StrCat("lookup #", random.random()));
    }
  }

  void test(benchmark::State& state) {
    for (auto _ : state) {
      Envoy::Random::RandomGeneratorImpl random;
      for (uint64_t i = 0; i < lookups_.size(); ++i) {
        recent_lookups_.lookup(lookups_[random.random() % lookups_.size()]);
      }
    }
  }

private:
  std::vector<std::string> lookups_;
  Envoy::Stats::RecentLookups recent_lookups_;
};

static void BM_LookupsMixed(benchmark::State& state) {
  RecentLookupsSpeedTest speed_test(1000, 500);
  speed_test.test(state);
}
BENCHMARK(BM_LookupsMixed);

static void BM_LookupsNoEvictions(benchmark::State& state) {
  RecentLookupsSpeedTest speed_test(1000, 1000);
  speed_test.test(state);
}
BENCHMARK(BM_LookupsNoEvictions);

static void BM_LookupsAllEvictions(benchmark::State& state) {
  RecentLookupsSpeedTest speed_test(1000, 10);
  speed_test.test(state);
}
BENCHMARK(BM_LookupsAllEvictions);

int main(int argc, char** argv) {
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Context logger_context(spdlog::level::warn,
                                        Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock, false);
  benchmark::Initialize(&argc, argv);

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
