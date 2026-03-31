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

// --- Joiner Key ---
class GaugeKeyUsingJoiner {
public:
  GaugeKeyUsingJoiner(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef borrowed_tags,
                      Stats::SymbolTable& symbolTable)
      : stat_name_(stat_name),
        joiner_storage_(Stats::StatName(), stat_name, borrowed_tags, symbolTable),
        borrowed_tags_(borrowed_tags) {}

  GaugeKeyUsingJoiner(GaugeKeyUsingJoiner&&) noexcept = default;
  GaugeKeyUsingJoiner& operator=(GaugeKeyUsingJoiner&&) noexcept = default;

  GaugeKeyUsingJoiner(const GaugeKeyUsingJoiner&) = delete;
  GaugeKeyUsingJoiner& operator=(const GaugeKeyUsingJoiner&) = delete;

  void makeOwned() {
    if (borrowed_tags_.has_value() && !owned_tags_.has_value()) {
      owned_tags_.emplace(borrowed_tags_.value());
      borrowed_tags_ = absl::nullopt;
    }
  }

  Stats::StatNameTagVectorOptConstRef tags() const {
    if (owned_tags_.has_value()) {
      return std::cref(owned_tags_.value());
    }
    return borrowed_tags_;
  }

  Stats::StatName statName() const { return stat_name_; }

  bool operator==(const GaugeKeyUsingJoiner& rhs) const { return joinedName() == rhs.joinedName(); }

  template <typename H> friend H AbslHashValue(H h, const GaugeKeyUsingJoiner& key) {
    return H::combine(std::move(h), key.joinedName());
  }

  Stats::StatName joinedName() const { return joiner_storage_.nameWithTags(); }

private:
  Stats::StatName stat_name_;
  Stats::TagUtility::TagStatNameJoiner joiner_storage_;
  absl::optional<Stats::StatNameTagVector> owned_tags_;
  Stats::StatNameTagVectorOptConstRef borrowed_tags_{absl::nullopt};
};

// --- Joiner-based AccessLogState ---
class AccessLogStateUsingJoiner {
public:
  AccessLogStateUsingJoiner(std::shared_ptr<StatsAccessLog> logger) : logger_(logger) {}

  void addInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                        Stats::Gauge::ImportMode import_mode, uint64_t value,
                        std::vector<Stats::StatNameDynamicStorage> tags_storage) {
    if (value == 0)
      return;

    GaugeKeyUsingJoiner key{stat_name, tags, logger_->scope().symbolTable()};
    auto it = inflight_gauges_.find(key);
    if (it == inflight_gauges_.end()) {
      auto [new_it, inserted] =
          inflight_gauges_.try_emplace(std::move(key), 0, import_mode, std::move(tags_storage));
      it = new_it;
    }
    it->second.value_ += value;
    logger_->scope().gaugeFromStatName(it->first.joinedName(), import_mode).add(value);
  }

  void removeInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                           Stats::Gauge::ImportMode import_mode, uint64_t value) {
    if (value == 0)
      return;

    GaugeKeyUsingJoiner key{stat_name, tags, logger_->scope().symbolTable()};
    auto it = inflight_gauges_.find(key);
    if (it != inflight_gauges_.end()) {
      it->second.value_ -= value;
      logger_->scope().gaugeFromStatName(it->first.joinedName(), import_mode).sub(value);
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
                     std::vector<Stats::StatNameDynamicStorage> tags_storage)
        : value_(value), import_mode_(import_mode), tags_storage_(std::move(tags_storage)) {}
    uint64_t value_;
    Stats::Gauge::ImportMode import_mode_;
    std::vector<Stats::StatNameDynamicStorage> tags_storage_;
  };
  absl::node_hash_map<GaugeKeyUsingJoiner, InflightGaugeNew> inflight_gauges_;
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
