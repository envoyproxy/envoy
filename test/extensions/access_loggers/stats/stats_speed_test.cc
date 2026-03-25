#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/thread_local_store.h"
#include "source/extensions/access_loggers/stats/stats.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

static void BM_StatsAccessLogAddSubtractGaugeWithTags(benchmark::State& state) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  Stats::SymbolTableImpl symbol_table;
  Stats::AllocatorImpl alloc(symbol_table);
  Stats::ThreadLocalStoreImpl store(alloc);
  Stats::StatNamePool pool(store.symbolTable());

  ON_CALL(context, statsScope()).WillByDefault(testing::ReturnRef(*store.rootScope()));

  envoy::extensions::access_loggers::stats::v3::Config config;
  AccessLog::FilterPtr filter = nullptr;
  auto logger = std::make_shared<StatsAccessLog>(config, context, std::move(filter),
                                                 std::vector<Formatter::CommandParserPtr>());
  auto access_log_state = std::make_shared<AccessLogState>(logger);

  Stats::StatName stat_name = pool.add("test_gauge");
  Stats::StatNameTagVector tags;
  tags.emplace_back(pool.add("tag_key_1"), pool.add("tag_value_1"));
  tags.emplace_back(pool.add("tag_key_2"), pool.add("tag_value_2"));
  tags.emplace_back(pool.add("tag_key_3"), pool.add("tag_value_3"));

  for (auto _ : state) { // NOLINT
    access_log_state->addInflightGauge(stat_name, tags, Stats::Gauge::ImportMode::Accumulate, 1,
                                       {});
    access_log_state->removeInflightGauge(stat_name, tags, Stats::Gauge::ImportMode::Accumulate, 1);
  }
}
BENCHMARK(BM_StatsAccessLogAddSubtractGaugeWithTags);

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
