#include <cstdint>
#include <memory>

#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"

#include "source/common/stats/filter.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/server.h"

#include "test/benchmark/main.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

class StatsSinkFlushSpeedTest {
public:
  StatsSinkFlushSpeedTest(size_t const num_stats)
      : pool_(symbol_table_), stats_allocator_(symbol_table_), stats_store_(stats_allocator_) {

    // Create counters
    for (uint64_t idx = 0; idx < num_stats; ++idx) {
      auto stat_name = pool_.add(absl::StrCat("counter.", idx));
      stats_store_.counterFromStatName(stat_name).inc();
    }
    // Create gauges
    /*for (uint64_t idx = 0; idx < num_stats; ++idx) {
      auto stat_name = pool_.add(absl::StrCat("gauge.", idx));
      stats_store_.gaugeFromStatName(stat_name, Stats::Gauge::ImportMode::NeverImport).set(idx);
      }

    // Create text readouts
    for (uint64_t idx = 0; idx < num_stats; ++idx) {
      auto stat_name = pool_.add(absl::StrCat("text_readout.", idx));
      stats_store_.textReadoutFromStatName(stat_name).set(absl::StrCat("text_readout.", idx));
      }*/
  }

  void testSinks(::benchmark::State& state) {
    for (auto _ : state) {
      UNREFERENCED_PARAMETER(_);
      std::list<Stats::SinkPtr> sinks;
      sinks.emplace_back(new testing::NiceMock<Stats::MockSink>());
      Server::InstanceUtil::flushMetricsToSinks(sinks, stats_store_, time_system_);
    }
  }

  void testAdmin(::benchmark::State& state) {
    for (auto _ : state) {
      UNREFERENCED_PARAMETER(_);

      uint32_t total_bytes = 0;
      const uint32_t page_size = state.range(0);

      // Get the all the pages.
      Stats::CounterSharedPtr last;
      std::vector<Stats::CounterSharedPtr> counters;
      Stats::StatsFilter<Stats::Counter> filter(stats_store_);
      while (true) {
        counters = filter.getFilteredStatsAfter(
            page_size, last == nullptr ? Stats::StatName() : last->statName());
        if (counters.empty()) {
          break;
        }
        last = counters[counters.size() - 1];
        total_bytes += counters.size();
      }
      ASSERT(total_bytes > 0);
    }
  }

private:
  Stats::SymbolTableImpl symbol_table_;
  Stats::StatNamePool pool_;
  Stats::AllocatorImpl stats_allocator_;
  Stats::ThreadLocalStoreImpl stats_store_;
  Event::SimulatedTimeSystem time_system_;
};

/*static void bmFlushToSinks(::benchmark::State& state) {
  // Skip expensive benchmarks for unit tests.
  if (benchmark::skipExpensiveBenchmarks() && state.range(0) > 100) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }

  StatsSinkFlushSpeedTest speed_test(state.range(0));
  speed_test.testSinks(state);
}
BENCHMARK(bmFlushToSinks)->Unit(::benchmark::kMillisecond)->RangeMultiplier(10)->Range(10, 1000000);
*/

static void bmAdmin(::benchmark::State& state) {
  const uint32_t num_stats = benchmark::skipExpensiveBenchmarks() ? 100 : (10*1000);
  StatsSinkFlushSpeedTest speed_test(num_stats);
  speed_test.testAdmin(state);
}
BENCHMARK(bmAdmin)->Unit(::benchmark::kMillisecond)->RangeMultiplier(10)->Range(10, 100);

} // namespace Envoy
