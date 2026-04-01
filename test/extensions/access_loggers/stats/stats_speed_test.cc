#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/tag_utility.h"
#include "source/common/stats/thread_local_store.h"
#include "source/extensions/access_loggers/stats/stats.h"

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

// --- Joiner-based AccessLogState ---
class AccessLogStateUsingJoiner {
public:
  AccessLogStateUsingJoiner(std::shared_ptr<StatsAccessLog> logger) : logger_(logger) {}

  void addInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                        Stats::Gauge::ImportMode import_mode, uint64_t value,
                        std::vector<Stats::StatNameDynamicStorage> tags_storage) {
    if (value == 0)
      return;

    Stats::TagUtility::TagStatNameJoiner joiner(Stats::StatName(), stat_name, tags,
                                                logger_->scope().symbolTable());
    Stats::StatName joined_name = joiner.nameWithTags();
    auto it = inflight_gauges_.find(joined_name);
    if (it == inflight_gauges_.end()) {
      auto [new_it, inserted] = inflight_gauges_.try_emplace(
          joined_name, 0, import_mode, std::move(tags_storage), std::move(joiner));
      it = new_it;
    }
    it->second.value_ += value;
    logger_->scope().gaugeFromStatName(joined_name, import_mode).add(value);
  }

  void removeInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                           Stats::Gauge::ImportMode import_mode, uint64_t value) {
    if (value == 0)
      return;

    Stats::TagUtility::TagStatNameJoiner joiner(Stats::StatName(), stat_name, tags,
                                                logger_->scope().symbolTable());
    Stats::StatName joined_name = joiner.nameWithTags();
    auto it = inflight_gauges_.find(joined_name);
    if (it != inflight_gauges_.end()) {
      it->second.value_ -= value;
      logger_->scope().gaugeFromStatName(joined_name, import_mode).sub(value);
      if (it->second.value_ == 0) {
        inflight_gauges_.erase(it);
      }
    }
  }

  size_t mapSize() const { return inflight_gauges_.size(); }

private:
  std::shared_ptr<StatsAccessLog> logger_;
  struct InflightGaugeNew {
    InflightGaugeNew(uint64_t value, Stats::Gauge::ImportMode import_mode,
                     std::vector<Stats::StatNameDynamicStorage> tags_storage,
                     Stats::TagUtility::TagStatNameJoiner&& joiner)
        : value_(value), import_mode_(import_mode), tags_storage_(std::move(tags_storage)),
          joiner_(std::move(joiner)) {}
    uint64_t value_;
    Stats::Gauge::ImportMode import_mode_;
    std::vector<Stats::StatNameDynamicStorage> tags_storage_;
    Stats::TagUtility::TagStatNameJoiner joiner_;
  };
  absl::node_hash_map<Stats::StatName, InflightGaugeNew> inflight_gauges_;
};

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
  const size_t length_selector = state.range(1);

  const char* const short_values[] = {"v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10"};
  const char* const long_values[] = {
      "val_01234567890123456789012345678901234567890123456789012301",
      "val_01234567890123456789012345678901234567890123456789012302",
      "val_01234567890123456789012345678901234567890123456789012303",
      "val_01234567890123456789012345678901234567890123456789012304",
      "val_01234567890123456789012345678901234567890123456789012305",
      "val_01234567890123456789012345678901234567890123456789012306",
      "val_01234567890123456789012345678901234567890123456789012307",
      "val_01234567890123456789012345678901234567890123456789012308",
      "val_01234567890123456789012345678901234567890123456789012309",
      "val_01234567890123456789012345678901234567890123456789012310"};
  const char* const* values = (length_selector == 0) ? short_values : long_values;

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
BENCHMARK(BM_AccessLogState)
    ->Args({/*tag_count=*/3, /*length_selector=*/0})
    ->Args({/*tag_count=*/10, /*length_selector=*/0})
    ->Args({/*tag_count=*/3, /*length_selector=*/1})
    ->Args({/*tag_count=*/10, /*length_selector=*/1});

// --- Joiner Benchmark ---
static void BM_AccessLogStateUsingJoiner(benchmark::State& state) {
  SharedBencherSetup setup;
  AccessLogStateUsingJoiner access_log_state(setup.logger_);
  runBenchmark(state, setup, access_log_state);
}
BENCHMARK(BM_AccessLogStateUsingJoiner)
    ->Args({/*tag_count=*/3, /*length_selector=*/0})
    ->Args({/*tag_count=*/10, /*length_selector=*/0})
    ->Args({/*tag_count=*/3, /*length_selector=*/1})
    ->Args({/*tag_count=*/10, /*length_selector=*/1});

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
