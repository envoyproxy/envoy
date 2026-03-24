#include "source/common/stats/thread_local_store.h"
#include "source/extensions/access_loggers/stats/stats.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

namespace {

class MockScopeWithGauge : public Stats::MockScope {
public:
  using Stats::MockScope::MockScope;

  MOCK_METHOD(Stats::Gauge&, gaugeFromStatNameWithTags,
              (const Stats::StatName& name, Stats::StatNameTagVectorOptConstRef tags,
               Stats::Gauge::ImportMode import_mode),
              (override));
};

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StatsAccessLogAddSubtractGauge(benchmark::State& state) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  NiceMock<Stats::MockStore> store;
  Stats::StatNamePool pool(store.symbolTable());
  NiceMock<MockScopeWithGauge> mock_scope(pool.add("prefix"), store);
  NiceMock<Stats::MockGauge> mock_gauge;

  auto inner_mock_scope = std::make_shared<NiceMock<MockScopeWithGauge>>(pool.add("inner"), store);
  ON_CALL(mock_scope, createScope_(_)).WillByDefault(testing::Return(inner_mock_scope));
  ON_CALL(*inner_mock_scope, gaugeFromStatNameWithTags(_, _, _))
      .WillByDefault(testing::ReturnRef(mock_gauge));
  ON_CALL(context, statsScope()).WillByDefault(testing::ReturnRef(mock_scope));

  envoy::extensions::access_loggers::stats::v3::Config config;
  AccessLog::FilterPtr filter = nullptr;
  auto logger = std::make_shared<StatsAccessLog>(config, context, std::move(filter),
                                                 std::vector<Formatter::CommandParserPtr>());
  auto access_log_state = std::make_shared<AccessLogState>(logger);

  Stats::StatName stat_name = pool.add("test_gauge");
  Stats::StatNameTagVector tags;

  for (auto _ : state) { // NOLINT
    access_log_state->addInflightGauge(stat_name, tags, Stats::Gauge::ImportMode::Accumulate, 1,
                                       {});
    access_log_state->removeInflightGauge(stat_name, tags, Stats::Gauge::ImportMode::Accumulate, 1);
  }
}
BENCHMARK(BM_StatsAccessLogAddSubtractGauge);

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
