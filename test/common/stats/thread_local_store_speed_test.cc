// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "common/stats/heap_stat_data.h"
#include "common/stats/stats_options_impl.h"
#include "common/stats/thread_local_store.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/thread_local/mocks.h"

#include "testing/base/public/benchmark.h"

#include "gmock/gmock.h"

// NOLINT(namespace-envoy)

class ThreadLocalStorePerf {
 public:
  ThreadLocalStorePerf() : store_(options_, heap_alloc_) {}

  ~ThreadLocalStorePerf() {
    store_.shutdownThreading();
  }

  void accessCounters() {
    Envoy::Stats::TestUtil::forEachSampleStat(
        1000, [this](absl::string_view name) { store_.counter(std::string(name)); });
  }

  void initThreading() {
    store_.initializeThreading(main_thread_dispatcher_, tls_);
  }

 private:
  Envoy::Stats::HeapStatDataAllocator heap_alloc_;
  Envoy::Stats::StatsOptionsImpl options_;
  Envoy::Stats::ThreadLocalStoreImpl store_;
  testing::NiceMock<Envoy::Event::MockDispatcher> main_thread_dispatcher_;
  testing::NiceMock<Envoy::ThreadLocal::MockInstance> tls_;
};

static void BM_StatsNoTls(benchmark::State& state) {
  ThreadLocalStorePerf context;
  for (auto _ : state) {
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsNoTls);

static void BM_StatsWithTls(benchmark::State& state) {
  ThreadLocalStorePerf context;
  context.initThreading();
  for (auto _ : state) {
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsWithTls);


// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
