#include "source/common/stats/symbol_table.h"
#include "envoy/upstream/upstream.h"
#include "test/benchmark/main.h"
#include "benchmark/benchmark.h"
#include "source/common/stats/isolated_store_impl.h"
#include "test/test_common/real_threads_test_helper.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/common/common/random_generator.h"
#include "source/common/stats/thread_local_store.h"

namespace Envoy {

namespace {

using Upstream::ClusterTrafficStats;

// Benchmark no-lazy-init on stats, the lazy init version is much faster since no allocation.
void benchmarkLazyInitCreation(::benchmark::State& state) {
  const bool lazy_init = state.range(0) == 1;
  const uint64_t num_stats = state.range(1);
  Stats::IsolatedStoreImpl stats_store;
  Upstream::ClusterTrafficStatNames stat_names{stats_store.symbolTable()};
  std::vector<Stats::ScopeSharedPtr> scopes;
  std::vector<std::shared_ptr<Stats::LazyInit<ClusterTrafficStats>>> lazy_stats;
  std::vector<std::shared_ptr<ClusterTrafficStats>> normal_stats;

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    for (uint64_t i = 0; i < num_stats; ++i) {
      std::string new_cluster_name = absl::StrCat("cluster_", i);
      auto scope = stats_store.createScope(new_cluster_name);
      scopes.push_back(scope);
      if (lazy_init) {
        lazy_stats.push_back(
            std::make_shared<Stats::LazyInit<ClusterTrafficStats>>(*scope, stat_names));
      } else {
        normal_stats.push_back(std::make_shared<ClusterTrafficStats>(stat_names, *scope));
      }
    }
  }
}

BENCHMARK(benchmarkLazyInitCreation)
    ->ArgsProduct({{0, 1}, {1000, 10000, 100000, 500000}})
    ->Unit(::benchmark::kMillisecond);

// Benchmark lazy-init of stats in same thread, mimicking main thread creation.
void benchmarkLazyInitCreationInstantiateSameThread(::benchmark::State& state) {
  const bool lazy_init = state.range(0) == 1;
  const uint64_t num_stats = state.range(1);
  Stats::IsolatedStoreImpl stats_store;
  Upstream::ClusterTrafficStatNames stat_names{stats_store.symbolTable()};
  std::vector<Stats::ScopeSharedPtr> scopes;
  std::vector<std::shared_ptr<Stats::LazyInit<ClusterTrafficStats>>> lazy_stats;
  std::vector<std::shared_ptr<ClusterTrafficStats>> normal_stats;

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    for (uint64_t i = 0; i < num_stats; ++i) {
      std::string new_cluster_name = absl::StrCat("cluster_", i);
      auto scope = stats_store.createScope(new_cluster_name);
      scopes.push_back(scope);
      if (lazy_init) {
        auto lazy_stat = std::make_shared<Stats::LazyInit<ClusterTrafficStats>>(*scope, stat_names);
        *(*lazy_stat);
        lazy_stats.push_back(std::move(lazy_stat));
      } else {
        normal_stats.push_back(std::make_shared<ClusterTrafficStats>(stat_names, *scope));
      }
    }
  }
}

BENCHMARK(benchmarkLazyInitCreationInstantiateSameThread)
    ->ArgsProduct({{0, 1}, {1000, 10000, 100000, 500000}})
    ->Unit(::benchmark::kMillisecond);

class ThreadLocalStoreNoMocksTestBase {
public:
  ThreadLocalStoreNoMocksTestBase()
      : alloc_(symbol_table_), store_(std::make_unique<Stats::ThreadLocalStoreImpl>(alloc_)),
        pool_(symbol_table_) {}
  ~ThreadLocalStoreNoMocksTestBase() {
    if (store_ != nullptr) {
      store_->shutdownThreading();
    }
  }

  Stats::StatName makeStatName(absl::string_view name) { return pool_.add(name); }

  Stats::SymbolTableImpl symbol_table_;
  Stats::AllocatorImpl alloc_;
  Stats::ThreadLocalStoreImplPtr store_;
  Stats::StatNamePool pool_;
};

class ThreadLocalRealThreadsTestBase : public Thread::RealThreadsTestHelper,
                                       public ThreadLocalStoreNoMocksTestBase {
public:
  ThreadLocalRealThreadsTestBase(uint32_t num_threads) : RealThreadsTestHelper(num_threads) {
    runOnMainBlocking([this]() { store_->initializeThreading(*main_dispatcher_, *tls_); });
  }

  ~ThreadLocalRealThreadsTestBase() {
    // TODO(chaoqin-li1123): clean this up when we figure out how to free the threading resources in
    // RealThreadsTestHelper.
    shutdownThreading();
    exitThreads([this]() { store_.reset(); });
  }

  void shutdownThreading() {
    runOnMainBlocking([this]() {
      if (!tls_->isShutdown()) {
        tls_->shutdownGlobalThreading();
      }
      store_->shutdownThreading();
      tls_->shutdownThread();
    });
  }
};

class MultiThreadLazyinitStatsTest : public ThreadLocalRealThreadsTestBase {
public:
  MultiThreadLazyinitStatsTest() : ThreadLocalRealThreadsTestBase(5) {}
};

// Benchmark lazy-init stats in different worker thread, mimicking worker threads creation.
void benchmarkLazyInitCreationInstantiateOnWorkerThreads(::benchmark::State& state) {
  const bool lazy_init = state.range(0) == 1;
  const uint64_t num_stats = state.range(1);
  Envoy::Event::Libevent::Global::initialize();
  MultiThreadLazyinitStatsTest test;
  std::vector<Stats::ScopeSharedPtr> scopes;
  std::vector<std::shared_ptr<Stats::LazyInit<ClusterTrafficStats>>> lazy_stats;
  std::vector<std::shared_ptr<ClusterTrafficStats>> normal_stats;
  Upstream::ClusterTrafficStatNames stat_names{test.store_->symbolTable()};

  for (auto _ : state) {           // NOLINT: Silences warning about dead store
    test.runOnMainBlocking([&]() { // Create stats on main-thread.
      for (uint64_t i = 0; i < num_stats; ++i) {
        std::string new_cluster_name = absl::StrCat("cluster_", i);
        auto scope = test.store_->createScope(new_cluster_name);
        scopes.push_back(scope);
        if (lazy_init) {
          lazy_stats.push_back(
              std::make_shared<Stats::LazyInit<ClusterTrafficStats>>(*scope, stat_names));
        } else {
          normal_stats.push_back(std::make_shared<ClusterTrafficStats>(stat_names, *scope));
        }
      }
    });
    std::atomic_int thread_idx = 0;
    test.runOnAllWorkersBlocking([&]() {
      int32_t batch_size = num_stats / 5;
      int t_idx = thread_idx++;
      uint64_t begin = t_idx * batch_size;
      uint64_t end = std::min(begin + batch_size, num_stats);
      for (uint64_t idx = begin; idx < end; ++idx) {
        if (lazy_init) {
          // Lazy-init on workers happen when the "index"-th stat instance is not created.
          ClusterTrafficStats& stats = *(*lazy_stats[idx]);
          UNREFERENCED_PARAMETER(stats);

        } else {
          ClusterTrafficStats& stats = *normal_stats[idx];
          UNREFERENCED_PARAMETER(stats);
        }
      }
    });
  }
}

BENCHMARK(benchmarkLazyInitCreationInstantiateOnWorkerThreads)
    ->ArgsProduct({{0, 1}, {1000, 10000, 100000, 500000}})
    ->Unit(::benchmark::kMillisecond);

// Benchmark mimicks that worker threads inc the stats.
void benchmarkLazyInitStatsAccess(::benchmark::State& state) {
  const bool lazy_init = state.range(0) == 1;
  const uint64_t num_stats = state.range(1);
  Envoy::Event::Libevent::Global::initialize();
  MultiThreadLazyinitStatsTest test;
  std::vector<Stats::ScopeSharedPtr> scopes;
  std::vector<std::shared_ptr<Stats::LazyInit<ClusterTrafficStats>>> lazy_stats;
  std::vector<std::shared_ptr<ClusterTrafficStats>> normal_stats;
  Upstream::ClusterTrafficStatNames stat_names{test.store_->symbolTable()};

  for (auto _ : state) {           // NOLINT: Silences warning about dead store
    test.runOnMainBlocking([&]() { // Create stats on main-thread.
      for (uint64_t i = 0; i < num_stats; ++i) {
        std::string new_cluster_name = absl::StrCat("cluster_", i);
        auto scope = test.store_->createScope(new_cluster_name);
        scopes.push_back(scope);
        if (lazy_init) {
          auto ptr = std::make_shared<Stats::LazyInit<ClusterTrafficStats>>(*scope, stat_names);
          *(*ptr);
          lazy_stats.push_back(std::move(ptr));
        } else {
          normal_stats.push_back(std::make_shared<ClusterTrafficStats>(stat_names, *scope));
        }
      }
    });
    test.runOnAllWorkersBlocking([&]() {
      // 50 x num_stats inc() calls.
      for (uint64_t idx = 0; idx < 10 * num_stats; ++idx) {
        if (lazy_init) {
          // Lazy-init on workers happen when the "index"-th stat instance is not created.
          ClusterTrafficStats& stats = *(*lazy_stats[idx]);
          stats.upstream_cx_active_.inc();

        } else {
          ClusterTrafficStats& stats = *normal_stats[idx];
          stats.upstream_cx_active_.inc();
        }
      }
    });
  }
}

BENCHMARK(benchmarkLazyInitStatsAccess)
    ->ArgsProduct({{0, 1}, {1000, 10000, 100000, 500000}})
    ->Unit(::benchmark::kMillisecond);

} // namespace

} // namespace Envoy