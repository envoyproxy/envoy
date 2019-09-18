// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.
//
// NOLINT(namespace-envoy)

#include "common/runtime/runtime_impl.h"
#include "common/stats/recent_lookups.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"

static void BM_Lookups(benchmark::State& state) {
  Envoy::Runtime::RandomGeneratorImpl random;
  const size_t vec_size = 1000;
  const size_t lookup_variants = 50;
  std::vector<std::string> lookups;
  for (size_t i = 0; i < vec_size; ++i) {
    lookups.push_back(absl::StrCat("lookup #", random.random() % lookup_variants));
  }
  Envoy::Stats::RecentLookups recent_lookups;
  size_t index = 0;
  for (auto _ : state) {
    recent_lookups.lookup(lookups[++index % vec_size]);
  }
}
BENCHMARK(BM_Lookups);

int main(int argc, char** argv) {
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Context logger_context(spdlog::level::warn,
                                        Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock);
  benchmark::Initialize(&argc, argv);

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
