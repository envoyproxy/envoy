#include <cstdint>
#include <memory>

#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"

#include "source/common/stats/thread_local_store.h"
#include "source/server/server.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

namespace Envoy {
class StatsSinkFlushSpeedTest {

public:
  StatsSinkFlushSpeedTest(uint64_t n_counters, uint64_t n_gauges, uint64_t n_text_readouts)
      : pool_(symbol_table_), stats_allocator_(symbol_table_), stats_store_(stats_allocator_) {

    sinks_.emplace_back(new testing::NiceMock<Stats::MockSink>());
    // Create counters
    for (uint64_t idx = 0; idx < n_counters; ++idx) {
      auto stat_name = pool_.add(absl::StrCat("counter.", idx));
      stats_store_.counterFromStatName(stat_name).inc();
    }
    // Create gauges
    for (uint64_t idx = 0; idx < n_gauges; ++idx) {
      auto stat_name = pool_.add(absl::StrCat("gauge.", idx));
      stats_store_.gaugeFromStatName(stat_name, Stats::Gauge::ImportMode::NeverImport).set(idx);
    }

    // Create text readouts
    for (uint64_t idx = 0; idx < n_text_readouts; ++idx) {
      auto stat_name = pool_.add(absl::StrCat("text_readout.", idx));
      stats_store_.textReadoutFromStatName(stat_name).set(absl::StrCat("text_readout.", idx));
    }
  }

  void test(benchmark::State& state) {
    for (auto _ : state) {
      UNREFERENCED_PARAMETER(state);
      Server::InstanceUtil::flushMetricsToSinks(sinks_, stats_store_, time_system_);
    }
  }

private:
  Stats::SymbolTableImpl symbol_table_;
  Stats::StatNamePool pool_;
  Stats::AllocatorImpl stats_allocator_;
  Stats::ThreadLocalStoreImpl stats_store_;
  std::list<Stats::SinkPtr> sinks_;
  Event::SimulatedTimeSystem time_system_;
};

static void bmLarge(benchmark::State& state) {
  uint64_t n_counters = 1000000, n_gauges = 1000000, n_text_readouts = 1000000;
  StatsSinkFlushSpeedTest speed_test(n_counters, n_gauges, n_text_readouts);
  speed_test.test(state);
}
BENCHMARK(bmLarge)->Unit(::benchmark::kMillisecond);

static void bmSmall(benchmark::State& state) {
  uint64_t n_counters = 10000, n_gauges = 10000, n_text_readouts = 10000;
  StatsSinkFlushSpeedTest speed_test(n_counters, n_gauges, n_text_readouts);
  speed_test.test(state);
}
BENCHMARK(bmSmall)->Unit(::benchmark::kMillisecond);

} // namespace Envoy
