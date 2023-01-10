#include "envoy/upstream/upstream.h"

#include "source/common/common/random_generator.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/lazy_init.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/thread_local_store.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/exe/process_wide.h"

#include "test/benchmark/main.h"
#include "test/common/stats/real_thread_test_base.h"
#include "test/test_common/real_threads_test_helper.h"

#include "benchmark/benchmark.h"

namespace Envoy {

namespace Stats {

using Upstream::ClusterTrafficStats;

class LazyInitStatsBenchmarkBase {
public:
  LazyInitStatsBenchmarkBase(bool lazy, const uint64_t n_clusters, Stats::Store& s)
      : lazy_init_(lazy), num_clusters_(n_clusters), stat_store_(s),
        stat_names_(stat_store_.symbolTable()) {}

  void createStats(bool defer_init) {
    for (uint64_t i = 0; i < num_clusters_; ++i) {
      std::string new_cluster_name = absl::StrCat("cluster_", i);
      auto scope = stat_store_.createScope(new_cluster_name);
      scopes_.push_back(scope);
      if (lazy_init_) {
        auto lazy_stat = std::make_shared<Stats::LazyInit<ClusterTrafficStats>>(stat_names_, scope);
        lazy_stats_.push_back(lazy_stat);
        if (!defer_init) {
          *(*lazy_stat);
        }
      } else {
        normal_stats_.push_back(std::make_shared<ClusterTrafficStats>(stat_names_, *scope));
      }
    }
  }

  const bool lazy_init_;
  const uint64_t num_clusters_;
  Stats::Store& stat_store_;
  std::vector<Stats::ScopeSharedPtr> scopes_;
  std::vector<std::shared_ptr<Stats::LazyInit<ClusterTrafficStats>>> lazy_stats_;
  std::vector<std::shared_ptr<ClusterTrafficStats>> normal_stats_;
  Upstream::ClusterTrafficStatNames stat_names_;
};

// Benchmark no-lazy-init on stats, the lazy init version is much faster since no allocation.
void benchmarkLazyInitCreation(::benchmark::State& state) {
  Stats::IsolatedStoreImpl stats_store;
  LazyInitStatsBenchmarkBase base(state.range(0) == 1, state.range(1), stats_store);

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    base.createStats(/*defer_init=*/true);
  }
}

BENCHMARK(benchmarkLazyInitCreation)
    ->ArgsProduct({{0, 1}, {1000, 10000, 20000}})
    ->Unit(::benchmark::kMillisecond);

// Benchmark lazy-init of stats in same thread, mimics main thread creation.
void benchmarkLazyInitCreationInstantiateSameThread(::benchmark::State& state) {
  Stats::IsolatedStoreImpl stats_store;
  LazyInitStatsBenchmarkBase base(state.range(0) == 1, state.range(1), stats_store);

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    base.createStats(/*defer_init=*/false);
  }
}

BENCHMARK(benchmarkLazyInitCreationInstantiateSameThread)
    ->ArgsProduct({{0, 1}, {1000, 10000, 20000}})
    ->Unit(::benchmark::kMillisecond);

class MultiThreadLazyinitStatsTest : public ThreadLocalRealThreadsTestBase,
                                     public LazyInitStatsBenchmarkBase {
public:
  MultiThreadLazyinitStatsTest(bool lazy, const uint64_t n_clusters)
      : ThreadLocalRealThreadsTestBase(5),
        LazyInitStatsBenchmarkBase(lazy, n_clusters, *ThreadLocalRealThreadsTestBase::store_) {}

  ~MultiThreadLazyinitStatsTest() {
    shutdownThreading();
    // First, wait for the main-dispatcher to initiate the cross-thread TLS cleanup.
    mainDispatchBlock();

    // Next, wait for all the worker threads to complete their TLS cleanup.
    tlsBlock();

    // Finally, wait for the final central-cache cleanup, which occurs on the main thread.
    mainDispatchBlock();
  }
};

// Benchmark lazy-init stats in different worker threads, mimics worker threads creation.
void benchmarkLazyInitCreationInstantiateOnWorkerThreads(::benchmark::State& state) {
  ProcessWide process_wide_; // Process-wide state setup/teardown (excluding grpc).
  MultiThreadLazyinitStatsTest test(state.range(0) == 1, state.range(1));

  for (auto _ : state) {           // NOLINT: Silences warning about dead store
    test.runOnMainBlocking([&]() { // Create stats on main-thread.
      test.createStats(/*defer_init=*/true);
    });

    std::atomic<int> thread_idx = 0;
    test.runOnAllWorkersBlocking([&]() {
      int32_t batch_size = test.num_clusters_ / 5;
      int t_idx = thread_idx++;
      uint64_t begin = t_idx * batch_size;
      uint64_t end = std::min(begin + batch_size, test.num_clusters_);
      for (uint64_t idx = begin; idx < end; ++idx) {
        // Instantiate the actual ClusterTrafficStats objects in worker threads, in batches to avoid
        // possible contention.
        if (test.lazy_init_) {
          // Lazy-init on workers happen when the "index"-th stat instance is not created.
          *(*test.lazy_stats_[idx]);
        } else {
          *test.normal_stats_[idx];
        }
      }
    });
  }
}

BENCHMARK(benchmarkLazyInitCreationInstantiateOnWorkerThreads)
    ->ArgsProduct({{0, 1}, {1000, 10000, 20000}})
    ->Unit(::benchmark::kMillisecond);

// Benchmark mimics that worker threads inc the stats.
void benchmarkLazyInitStatsAccess(::benchmark::State& state) {
  ProcessWide process_wide_; // Process-wide state setup/teardown (excluding grpc).
  MultiThreadLazyinitStatsTest test(state.range(0) == 1, state.range(1));

  for (auto _ : state) {           // NOLINT: Silences warning about dead store
    test.runOnMainBlocking([&]() { // Create stats on main-thread.
      test.createStats(/*defer_init=*/false);
    });
    test.runOnAllWorkersBlocking([&]() {
      // 50 x num_clusters_ inc() calls.
      for (uint64_t idx = 0; idx < 10 * test.num_clusters_; ++idx) {
        if (test.lazy_init_) {
          ClusterTrafficStats& stats = *(*test.lazy_stats_[idx % test.num_clusters_]);
          stats.upstream_cx_active_.inc();
        } else {
          ClusterTrafficStats& stats = *test.normal_stats_[idx % test.num_clusters_];
          stats.upstream_cx_active_.inc();
        }
      }
    });
  }
}

BENCHMARK(benchmarkLazyInitStatsAccess)
    ->ArgsProduct({{0, 1}, {1000, 10000, 20000}})
    ->Unit(::benchmark::kMillisecond);

} // namespace Stats

} // namespace Envoy
