#include "source/common/common/random_generator.h"
#include "source/common/stats/deferred_creation.h"
#include "source/common/stats/isolated_store_impl.h"
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

// Creates a copy of Upstream::ALL_CLUSTER_TRAFFIC_STATS, such that we have a stable
// set of stats for performance test.
#define AWESOME_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)                           \
  COUNTER(bind_errors)                                                                             \
  COUNTER(original_dst_host_invalid)                                                               \
  COUNTER(retry_or_shadow_abandoned)                                                               \
  COUNTER(upstream_cx_close_notify)                                                                \
  COUNTER(upstream_cx_connect_attempts_exceeded)                                                   \
  COUNTER(upstream_cx_connect_fail)                                                                \
  COUNTER(upstream_cx_connect_timeout)                                                             \
  COUNTER(upstream_cx_connect_with_0_rtt)                                                          \
  COUNTER(upstream_cx_destroy)                                                                     \
  COUNTER(upstream_cx_destroy_local)                                                               \
  COUNTER(upstream_cx_destroy_local_with_active_rq)                                                \
  COUNTER(upstream_cx_destroy_remote)                                                              \
  COUNTER(upstream_cx_destroy_remote_with_active_rq)                                               \
  COUNTER(upstream_cx_destroy_with_active_rq)                                                      \
  COUNTER(upstream_cx_http1_total)                                                                 \
  COUNTER(upstream_cx_http2_total)                                                                 \
  COUNTER(upstream_cx_http3_total)                                                                 \
  COUNTER(upstream_cx_idle_timeout)                                                                \
  COUNTER(upstream_cx_max_duration_reached)                                                        \
  COUNTER(upstream_cx_max_requests)                                                                \
  COUNTER(upstream_cx_none_healthy)                                                                \
  COUNTER(upstream_cx_overflow)                                                                    \
  COUNTER(upstream_cx_pool_overflow)                                                               \
  COUNTER(upstream_cx_protocol_error)                                                              \
  COUNTER(upstream_cx_rx_bytes_total)                                                              \
  COUNTER(upstream_cx_total)                                                                       \
  COUNTER(upstream_cx_tx_bytes_total)                                                              \
  COUNTER(upstream_flow_control_backed_up_total)                                                   \
  COUNTER(upstream_flow_control_drained_total)                                                     \
  COUNTER(upstream_flow_control_paused_reading_total)                                              \
  COUNTER(upstream_flow_control_resumed_reading_total)                                             \
  COUNTER(upstream_internal_redirect_failed_total)                                                 \
  COUNTER(upstream_internal_redirect_succeeded_total)                                              \
  COUNTER(upstream_rq_cancelled)                                                                   \
  COUNTER(upstream_rq_completed)                                                                   \
  COUNTER(upstream_rq_maintenance_mode)                                                            \
  COUNTER(upstream_rq_max_duration_reached)                                                        \
  COUNTER(upstream_rq_pending_failure_eject)                                                       \
  COUNTER(upstream_rq_pending_overflow)                                                            \
  COUNTER(upstream_rq_pending_total)                                                               \
  COUNTER(upstream_rq_0rtt)                                                                        \
  COUNTER(upstream_rq_per_try_timeout)                                                             \
  COUNTER(upstream_rq_per_try_idle_timeout)                                                        \
  COUNTER(upstream_rq_retry)                                                                       \
  COUNTER(upstream_rq_retry_backoff_exponential)                                                   \
  COUNTER(upstream_rq_retry_backoff_ratelimited)                                                   \
  COUNTER(upstream_rq_retry_limit_exceeded)                                                        \
  COUNTER(upstream_rq_retry_overflow)                                                              \
  COUNTER(upstream_rq_retry_success)                                                               \
  COUNTER(upstream_rq_rx_reset)                                                                    \
  COUNTER(upstream_rq_timeout)                                                                     \
  COUNTER(upstream_rq_total)                                                                       \
  COUNTER(upstream_rq_tx_reset)                                                                    \
  COUNTER(upstream_http3_broken)                                                                   \
  GAUGE(upstream_cx_active, Accumulate)                                                            \
  GAUGE(upstream_cx_rx_bytes_buffered, Accumulate)                                                 \
  GAUGE(upstream_cx_tx_bytes_buffered, Accumulate)                                                 \
  GAUGE(upstream_rq_active, Accumulate)                                                            \
  GAUGE(upstream_rq_pending_active, Accumulate)                                                    \
  HISTOGRAM(upstream_cx_connect_ms, Milliseconds)                                                  \
  HISTOGRAM(upstream_cx_length_ms, Milliseconds)

MAKE_STAT_NAMES_STRUCT(AwesomeStatNames, AWESOME_STATS);
MAKE_STATS_STRUCT(AwesomeStats, AwesomeStatNames, AWESOME_STATS);

class DeferredCreationStatsBenchmarkBase {
public:
  DeferredCreationStatsBenchmarkBase(bool lazy, const uint64_t n_clusters, Store& s)
      : deferred_creation_(lazy), num_clusters_(n_clusters), stat_store_(s),
        stat_names_(stat_store_.symbolTable()) {}

  void createStats(bool defer_init) {
    for (uint64_t i = 0; i < num_clusters_; ++i) {
      std::string new_cluster_name = absl::StrCat("cluster_", i);
      ScopeSharedPtr scope = stat_store_.createScope(new_cluster_name);
      scopes_.push_back(scope);
      auto lazy_stat = std::make_shared<DeferredCreationCompatibleStats<AwesomeStats>>(
          createDeferredCompatibleStats<AwesomeStats>(scope, stat_names_, deferred_creation_));
      lazy_stats_.push_back(lazy_stat);
      if (!defer_init) {
        *(*lazy_stat);
      }
    }
  }

  const bool deferred_creation_;
  const uint64_t num_clusters_;
  Store& stat_store_;
  std::vector<ScopeSharedPtr> scopes_;
  std::vector<std::shared_ptr<DeferredCreationCompatibleStats<AwesomeStats>>> lazy_stats_;
  AwesomeStatNames stat_names_;
};

// Benchmark no-lazy-init on stats, the lazy init version is much faster since no allocation.
void benchmarkDeferredCreationCreation(::benchmark::State& state) {
  if (benchmark::skipExpensiveBenchmarks() && state.range(1) > 2000) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  IsolatedStoreImpl stats_store;
  DeferredCreationStatsBenchmarkBase base(state.range(0) == 1, state.range(1), stats_store);

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    base.createStats(/*defer_init=*/true);
  }
}

BENCHMARK(benchmarkDeferredCreationCreation)
    ->ArgsProduct({{0, 1}, {1000, 2000, 5000, 10000, 20000}})
    ->Unit(::benchmark::kMillisecond);

// Benchmark lazy-init of stats in same thread, mimics main thread creation.
void benchmarkDeferredCreationCreationInstantiateSameThread(::benchmark::State& state) {
  if (benchmark::skipExpensiveBenchmarks() && state.range(1) > 2000) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  IsolatedStoreImpl stats_store;
  DeferredCreationStatsBenchmarkBase base(state.range(0) == 1, state.range(1), stats_store);

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    base.createStats(/*defer_init=*/false);
  }
}

BENCHMARK(benchmarkDeferredCreationCreationInstantiateSameThread)
    ->ArgsProduct({{0, 1}, {1000, 2000, 5000, 10000, 20000}})
    ->Unit(::benchmark::kMillisecond);

class MultiThreadDeferredCreationStatsTest : public ThreadLocalRealThreadsMixin,
                                             public DeferredCreationStatsBenchmarkBase {
public:
  MultiThreadDeferredCreationStatsTest(bool lazy, const uint64_t n_clusters)
      : ThreadLocalRealThreadsMixin(5),
        DeferredCreationStatsBenchmarkBase(lazy, n_clusters, *ThreadLocalRealThreadsMixin::store_) {
  }

  ~MultiThreadDeferredCreationStatsTest() {
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
void benchmarkDeferredCreationCreationInstantiateOnWorkerThreads(::benchmark::State& state) {
  if (benchmark::skipExpensiveBenchmarks() && state.range(1) > 2000) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  ProcessWide process_wide_; // Process-wide state setup/teardown (excluding grpc).
  MultiThreadDeferredCreationStatsTest test(state.range(0) == 1, state.range(1));

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
        // Instantiate the actual AwesomeStats objects in worker threads, in batches to avoid
        // possible contention.
        if (test.deferred_creation_) {
          // Lazy-init on workers happen when the "index"-th stat instance is not created.
          *(*test.lazy_stats_[idx]);
        }
      }
    });
  }
}

BENCHMARK(benchmarkDeferredCreationCreationInstantiateOnWorkerThreads)
    ->ArgsProduct({{0, 1}, {1000, 2000, 5000, 10000, 20000}})
    ->Unit(::benchmark::kMillisecond);

// Benchmark mimics that worker threads inc the stats.
void benchmarkDeferredCreationStatsAccess(::benchmark::State& state) {
  if (benchmark::skipExpensiveBenchmarks() && state.range(1) > 2000) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  ProcessWide process_wide_; // Process-wide state setup/teardown (excluding grpc).
  MultiThreadDeferredCreationStatsTest test(state.range(0) == 1, state.range(1));

  for (auto _ : state) {           // NOLINT: Silences warning about dead store
    test.runOnMainBlocking([&]() { // Create stats on main-thread.
      test.createStats(/*defer_init=*/false);
    });
    test.runOnAllWorkersBlocking([&]() {
      // 50 x num_clusters_ inc() calls.
      for (uint64_t idx = 0; idx < 10 * test.num_clusters_; ++idx) {
        AwesomeStats& stats = *(*test.lazy_stats_[idx % test.num_clusters_]);
        stats.upstream_cx_active_.inc();
      }
    });
  }
}

BENCHMARK(benchmarkDeferredCreationStatsAccess)
    ->ArgsProduct({{0, 1}, {1000, 2000, 5000, 10000, 20000}})
    ->Unit(::benchmark::kMillisecond);

} // namespace Stats

} // namespace Envoy
