#include <cstdint>
#include <memory>

#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"

#include "source/common/stats/thread_local_store.h"
#include "source/server/server.h"

#include "test/benchmark/main.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

// Override the one method used by this test so that using a mock doesn't affect performance.
class FastMockClusterManager : public testing::StrictMock<Upstream::MockClusterManager> {
public:
  ClusterInfoMaps clusters() const override { return ClusterInfoMaps{}; }
};

class TestSinkPredicates : public Stats::SinkPredicates {
public:
  bool includeCounter(const Stats::Counter&) override { return (++num_counters_) % 10 == 0; }
  bool includeGauge(const Stats::Gauge&) override { return (++num_gauges_) % 10 == 0; }
  bool includeTextReadout(const Stats::TextReadout&) override {
    return (++num_text_readouts_) % 10 == 0;
  }
  bool includeHistogram(const Stats::Histogram&) override { return (++num_histograms_) % 10 == 0; }

private:
  size_t num_counters_ = 0;
  size_t num_gauges_ = 0;
  size_t num_text_readouts_ = 0;
  size_t num_histograms_ = 0;
};

class StatsSinkFlushSpeedTest {
public:
  StatsSinkFlushSpeedTest(size_t const num_stats, bool set_sink_predicates = false)
      : pool_(symbol_table_), stats_allocator_(symbol_table_), stats_store_(stats_allocator_) {
    if (set_sink_predicates) {
      stats_store_.setSinkPredicates(
          std::unique_ptr<Stats::SinkPredicates>{std::make_unique<TestSinkPredicates>()});
    }

    // Create counters
    for (uint64_t idx = 0; idx < num_stats; ++idx) {
      auto stat_name = pool_.add(absl::StrCat("counter.", idx));
      stats_store_.rootScope()->counterFromStatName(stat_name).inc();
    }
    // Create gauges
    for (uint64_t idx = 0; idx < num_stats; ++idx) {
      auto stat_name = pool_.add(absl::StrCat("gauge.", idx));
      stats_store_.rootScope()
          ->gaugeFromStatName(stat_name, Stats::Gauge::ImportMode::NeverImport)
          .set(idx);
    }

    // Create text readouts
    for (uint64_t idx = 0; idx < num_stats; ++idx) {
      auto stat_name = pool_.add(absl::StrCat("text_readout.", idx));
      stats_store_.rootScope()->textReadoutFromStatName(stat_name).set(
          absl::StrCat("text_readout.", idx));
    }

    // Create histograms
    for (uint64_t idx = 0; idx < num_stats; ++idx) {
      std::string stat_name(absl::StrCat("histogram.", idx));
      stats_store_.histogramFromString(stat_name, Stats::Histogram::Unit::Unspecified);
    }
  }

  void test(::benchmark::State& state) {
    for (auto _ : state) {
      UNREFERENCED_PARAMETER(_);
      std::list<Stats::SinkPtr> sinks;
      sinks.emplace_back(new testing::NiceMock<Stats::MockSink>());
      Server::InstanceUtil::flushMetricsToSinks(sinks, stats_store_, cm_, time_system_);
    }
  }

private:
  Stats::SymbolTableImpl symbol_table_;
  Stats::StatNamePool pool_;
  Stats::AllocatorImpl stats_allocator_;
  Stats::ThreadLocalStoreImpl stats_store_;
  Event::SimulatedTimeSystem time_system_;
  FastMockClusterManager cm_;
};

static void bmFlushToSinks(::benchmark::State& state) {
  // Skip expensive benchmarks for unit tests.
  if (benchmark::skipExpensiveBenchmarks() && state.range(0) > 100) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  StatsSinkFlushSpeedTest speed_test(state.range(0));
  speed_test.test(state);
}

static void bmFlushToSinksWithPredicatesSet(::benchmark::State& state) {
  // Skip expensive benchmarks for unit tests.
  if (benchmark::skipExpensiveBenchmarks() && state.range(0) > 100) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  StatsSinkFlushSpeedTest speed_test(state.range(0), true);
  speed_test.test(state);
}

BENCHMARK(bmFlushToSinks)->Unit(::benchmark::kMillisecond)->RangeMultiplier(10)->Range(10, 1000000);
BENCHMARK(bmFlushToSinksWithPredicatesSet)
    ->Unit(::benchmark::kMillisecond)
    ->RangeMultiplier(10)
    ->Range(10, 1000000);

} // namespace Envoy
