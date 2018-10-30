// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"
#include "common/stats/heap_stat_data.h"
#include "common/stats/stats_options_impl.h"
#include "common/stats/tag_producer_impl.h"
#include "common/stats/thread_local_store.h"
#include "common/thread_local/thread_local_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/simulated_time_system.h"

#include "testing/base/public/benchmark.h"

namespace Envoy {

class ThreadLocalStorePerf {
public:
  ThreadLocalStorePerf() : store_(options_, heap_alloc_) {
    store_.setTagProducer(std::make_unique<Stats::TagProducerImpl>(stats_config_));
  }

  ~ThreadLocalStorePerf() {
    store_.shutdownThreading();
    if (tls_) {
      tls_->shutdownGlobalThreading();
    }
  }

  void accessCounters() {
    Stats::TestUtil::forEachSampleStat(
        1000, [this](absl::string_view name) { store_.counter(std::string(name)); });
  }

  void initThreading() {
    dispatcher_ = std::make_unique<Event::DispatcherImpl>(time_system_);
    tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
    store_.initializeThreading(*dispatcher_, *tls_);
  }

private:
  Stats::StatsOptionsImpl options_;
  Event::SimulatedTimeSystem time_system_;
  Stats::HeapStatDataAllocator heap_alloc_;
  std::unique_ptr<Event::DispatcherImpl> dispatcher_;
  std::unique_ptr<ThreadLocal::InstanceImpl> tls_;
  Stats::ThreadLocalStoreImpl store_;
  envoy::config::metrics::v2::StatsConfig stats_config_;
};

} // namespace Envoy

// Tests the single-threaded performance of the thread-local-store stats caches
// without having initialized tls.
static void BM_StatsNoTls(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;

  for (auto _ : state) {
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsNoTls);

// Tests the single-threaded performance of the thread-local-store stats caches
// with tls. Note that this test is still single-threaded, and so there's only
// one replica of the tls cache.
static void BM_StatsWithTls(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;
  context.initThreading();

  for (auto _ : state) {
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsWithTls);

// TODO(jmarantz): add multi-threaded variant of this test, that aggressively
// looks up stats in multiple threads to try to trigger contention issues.

// TODO(jmarantz): add version using the RawStatDataAllocator, or better yet,
// the full hot-restart mechanism so that actual shared-memory is used.

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);

  Envoy::Event::Libevent::Global::initialize();
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
