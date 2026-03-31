#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/tag_utility.h"
#include "source/common/stats/thread_local_store.h"
#include "source/extensions/access_loggers/stats/stats.h"

#include "test/extensions/access_loggers/stats/joiner_state.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "absl/container/node_hash_map.h"
#include "benchmark/benchmark.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

// --- Shared Setup ---
struct SharedBencherSetup {
  SharedBencherSetup() : pool(store.symbolTable()) {
    ON_CALL(context, statsScope()).WillByDefault(testing::ReturnRef(*store.rootScope()));

    stat_name_ = pool.add("test_gauge");

    tag_keys_.emplace_back(pool.add("tag_key_1"));
    tag_keys_.emplace_back(pool.add("tag_key_2"));
    tag_keys_.emplace_back(pool.add("tag_key_3"));
    tag_keys_.emplace_back(pool.add("tag_key_4"));
    tag_keys_.emplace_back(pool.add("tag_key_5"));
    tag_keys_.emplace_back(pool.add("tag_key_6"));
    tag_keys_.emplace_back(pool.add("tag_key_7"));
    tag_keys_.emplace_back(pool.add("tag_key_8"));
    tag_keys_.emplace_back(pool.add("tag_key_9"));
    tag_keys_.emplace_back(pool.add("tag_key_10"));

    envoy::extensions::access_loggers::stats::v3::Config config;
    AccessLog::FilterPtr filter = nullptr;
    logger_ = std::make_shared<StatsAccessLog>(config, context, std::move(filter),
                                               std::vector<Formatter::CommandParserPtr>());
  }

  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  Stats::SymbolTableImpl symbol_table;
  Stats::AllocatorImpl alloc{symbol_table};
  Stats::ThreadLocalStoreImpl store{alloc};
  Stats::StatNamePool pool;

  Stats::StatName stat_name_;
  std::vector<Stats::StatName> tag_keys_;
  std::shared_ptr<StatsAccessLog> logger_;
};

// --- Shared Benchmark Template ---
template <typename T>
static void runBenchmark(benchmark::State& state, SharedBencherSetup& setup, T& access_log_state) {
  const size_t tag_count = state.range(0);
  const char* const values[] = {"val1", "val2", "val3", "val4", "val5",
                                "val6", "val7", "val8", "val9", "val10"};
  for (auto _ : state) { // NOLINT
    std::vector<Stats::StatNameDynamicStorage> loop_storage;
    Stats::StatNameTagVector loop_tags;

    for (size_t i = 0; i < tag_count; ++i) {
      loop_storage.emplace_back(values[i], setup.store.symbolTable());
      loop_tags.emplace_back(setup.tag_keys_[i], loop_storage.back().statName());
    }

    access_log_state.addInflightGauge(setup.stat_name_, loop_tags,
                                      Stats::Gauge::ImportMode::Accumulate, 1,
                                      std::move(loop_storage));

    access_log_state.removeInflightGauge(setup.stat_name_, loop_tags,
                                         Stats::Gauge::ImportMode::Accumulate, 1);
  }
}

// --- Reality Benchmark ---
static void BM_AccessLogState(benchmark::State& state) {
  SharedBencherSetup setup;
  auto access_log_state = std::make_shared<AccessLogState>(setup.logger_);
  runBenchmark(state, setup, *access_log_state);
}
BENCHMARK(BM_AccessLogState)->Arg(1)->Arg(5)->Arg(10);

// --- Joiner Benchmark ---
static void BM_AccessLogStateUsingJoiner(benchmark::State& state) {
  SharedBencherSetup setup;
  AccessLogStateUsingJoiner access_log_state(setup.logger_);
  runBenchmark(state, setup, access_log_state);
}
BENCHMARK(BM_AccessLogStateUsingJoiner)->Arg(1)->Arg(5)->Arg(10);

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
