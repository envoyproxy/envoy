// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/stats_matcher_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/tag_producer_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/common/thread_local/thread_local_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {

class ThreadLocalStorePerf {
public:
  ThreadLocalStorePerf()
      : heap_alloc_(symbol_table_), store_(heap_alloc_),
        api_(Api::createApiForTest(store_, time_system_)) {
    const Stats::TagVector tags;
    store_.setTagProducer(Stats::TagProducerImpl::createTagProducer(stats_config_, tags).value());

    Stats::TestUtil::forEachSampleStat(1000, true, [this](absl::string_view name) {
      stat_names_.push_back(std::make_unique<Stats::StatNameManagedStorage>(name, symbol_table_));
    });
  }

  ~ThreadLocalStorePerf() {
    if (tls_) {
      tls_->shutdownGlobalThreading();
    }
    store_.shutdownThreading();
    if (tls_) {
      tls_->shutdownThread();
    }
    if (dispatcher_) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  void accessCounters() {
    Stats::Scope& scope = *store_.rootScope();
    for (auto& stat_name_storage : stat_names_) {
      scope.counterFromStatName(stat_name_storage->statName());
    }
  }

  void initThreading() {
    if (!Envoy::Event::Libevent::Global::initialized()) {
      Envoy::Event::Libevent::Global::initialize();
    }
    dispatcher_ = api_->allocateDispatcher("test_thread");
    tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
    tls_->registerThread(*dispatcher_, true);
    store_.initializeThreading(*dispatcher_, *tls_);
  }

  void initPrefixRejections(const std::string& prefix) {
    stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
        prefix);
    store_.setStatsMatcher(
        std::make_unique<Stats::StatsMatcherImpl>(stats_config_, symbol_table_, context_));
  }

private:
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::SymbolTableImpl symbol_table_;
  Event::SimulatedTimeSystem time_system_;
  Stats::AllocatorImpl heap_alloc_;
  Event::DispatcherPtr dispatcher_;
  ThreadLocal::InstanceImplPtr tls_;
  Stats::ThreadLocalStoreImpl store_;
  Api::ApiPtr api_;
  envoy::config::metrics::v3::StatsConfig stats_config_;
  std::vector<std::unique_ptr<Stats::StatNameManagedStorage>> stat_names_;
};

} // namespace Envoy

// Tests the single-threaded performance of the thread-local-store stats caches
// without having initialized tls.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsNoTls(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;

  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsNoTls);

// Tests the single-threaded performance of the thread-local-store stats caches
// with tls. Note that this test is still single-threaded, and so there's only
// one replica of the tls cache.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsWithTls(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;
  context.initThreading();

  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsWithTls);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsWithTlsAndRejectionsWithDot(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;
  context.initThreading();
  context.initPrefixRejections("cluster.");

  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsWithTlsAndRejectionsWithDot);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsWithTlsAndRejectionsWithoutDot(benchmark::State& state) {
  Envoy::ThreadLocalStorePerf context;
  context.initThreading();
  context.initPrefixRejections("cluster");

  for (auto _ : state) { // NOLINT
    context.accessCounters();
  }
}
BENCHMARK(BM_StatsWithTlsAndRejectionsWithoutDot);

// TODO(jmarantz): add multi-threaded variant of this test, that aggressively
// looks up stats in multiple threads to try to trigger contention issues.
