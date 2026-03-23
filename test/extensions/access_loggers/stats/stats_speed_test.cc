#include "source/common/stats/symbol_table.h"
#include "source/common/stats/tag_utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include <utility>
#include <iostream>

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

// Definition of GaugeKey for the old approach
struct GaugeKey {
  Stats::StatName stat_name;
  Stats::StatNameTagVectorOptConstRef tags;
  Stats::Gauge::ImportMode import_mode;

  bool operator==(const GaugeKey& other) const {
    if (import_mode != other.import_mode || stat_name != other.stat_name) {
      return false;
    }
    if (tags.has_value() != other.tags.has_value()) {
      return false;
    }
    if (!tags.has_value()) {
      return true;
    }
    return tags.value().get() == other.tags.value().get();
  }

  template <typename H> friend H AbslHashValue(H h, const GaugeKey& k) {
    if (k.tags.has_value()) {
      h = H::combine(std::move(h), k.stat_name, k.tags.value().get(), k.import_mode);
    } else {
      h = H::combine(std::move(h), k.stat_name, 0, k.import_mode);
    }
    return h;
  }
};

// --- New Approach Class ---
class AccessLogStateNewTest {
public:
  AccessLogStateNewTest(Stats::Scope& scope) : scope_(scope) {}

  static void printSizes() {
    std::cout << "sizeof(AccessLogStateNewTest::InflightGaugeNew): " << sizeof(InflightGaugeNew) << "\n";
  }

  void addInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                        Stats::Gauge::ImportMode import_mode, uint64_t value) {
    if (value == 0) return;

    Stats::TagUtility::TagStatNameJoiner joiner(Stats::StatName(), stat_name, tags, scope_.symbolTable());
    Stats::StatName joined_name = joiner.nameWithTags();

    auto it = inflight_gauges_.find(joined_name);
    if (it == inflight_gauges_.end()) {
      Stats::StatName persistent_joined_name = joiner.nameWithTags();
      auto [new_it, inserted] = inflight_gauges_.emplace(
          persistent_joined_name, InflightGaugeNew{{}, 0, import_mode, std::move(joiner)});
      it = new_it;
    }
    it->second.value_ += value;
    scope_.gaugeFromStatName(joined_name, import_mode).add(value);
  }

  void removeInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                           Stats::Gauge::ImportMode import_mode, uint64_t value) {
    if (value == 0) return;

    Stats::TagUtility::TagStatNameJoiner joiner(Stats::StatName(), stat_name, tags, scope_.symbolTable());
    Stats::StatName joined_name = joiner.nameWithTags();

    Stats::Gauge& gauge_stat = scope_.gaugeFromStatName(joined_name, import_mode);

    auto it = inflight_gauges_.find(joined_name);
    if (it != inflight_gauges_.end()) {
      it->second.value_ -= value;
      gauge_stat.sub(value);
      if (it->second.value_ == 0) {
        inflight_gauges_.erase(it);
      }
    }
  }

private:
  Stats::Scope& scope_;
  struct InflightGaugeNew {
    std::vector<Stats::StatNameDynamicStorage> tags_storage_;
    uint64_t value_;
    Stats::Gauge::ImportMode import_mode_;
    Stats::TagUtility::TagStatNameJoiner joiner_;
  };
  absl::flat_hash_map<Stats::StatName, InflightGaugeNew> inflight_gauges_;
};

// --- Old Approach Class ---
class AccessLogStateOldTest {
public:
  AccessLogStateOldTest(Stats::Scope& scope) : scope_(scope) {}

  static void printSizes() {
    std::cout << "sizeof(AccessLogStateOldTest::InflightGaugeOld): " << sizeof(InflightGaugeOld) << "\n";
  }

  void addInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                        Stats::Gauge::ImportMode import_mode, uint64_t value) {
    if (value == 0) return;

    GaugeKey key{stat_name, tags, import_mode};
    auto it = inflight_gauges_.find(key);
    if (it == inflight_gauges_.end()) {
      auto [new_it, inserted] = inflight_gauges_.emplace(key, InflightGaugeOld{{}, 0});
      it = new_it;
    }
    it->second.value_ += value;
    scope_.gaugeFromStatNameWithTags(stat_name, tags, import_mode).add(value);
  }

  void removeInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                           Stats::Gauge::ImportMode import_mode, uint64_t value) {
    if (value == 0) return;

    GaugeKey key{stat_name, tags, import_mode};
    Stats::Gauge& gauge_stat = scope_.gaugeFromStatNameWithTags(stat_name, tags, import_mode);

    auto it = inflight_gauges_.find(key);
    if (it != inflight_gauges_.end()) {
      it->second.value_ -= value;
      gauge_stat.sub(value);
      if (it->second.value_ == 0) {
        inflight_gauges_.erase(it);
      }
    }
  }

private:
  Stats::Scope& scope_;
  struct InflightGaugeOld {
    std::vector<Stats::StatNameDynamicStorage> tags_storage_;
    uint64_t value_;
  };
  absl::flat_hash_map<GaugeKey, InflightGaugeOld> inflight_gauges_;
};


// --- Common Setup ---
struct BencherSetup {
  BencherSetup() : pool(symbol_table) {
    static bool printed = false;
    if (!printed) {
      printed = true;
      std::cout << "--- Sizeof Info ---\n";
      std::cout << "sizeof(GaugeKey): " << sizeof(GaugeKey) << "\n";
      AccessLogStateNewTest::printSizes();
      AccessLogStateOldTest::printSizes();
      std::cout << "--------------------\n";
    }
    stat_name_ = pool.add("foo.bar");
    tag_name_ = pool.add("tag");
    tag_value_ = pool.add("val");
    tags_.emplace_back(tag_name_, tag_value_);
  }

  Stats::SymbolTableImpl symbol_table;
  Stats::StatNamePool pool;
  Stats::StatName stat_name_;
  Stats::StatName tag_name_;
  Stats::StatName tag_value_;
  Stats::StatNameTagVector tags_;
};

// --- Benchmarks ---

static void bmAccessLogStateNewAdd(benchmark::State& state) {
  BencherSetup setup;
  Stats::IsolatedStoreImpl store(setup.symbol_table);
  AccessLogStateNewTest access_log_state(*store.rootScope());
  Stats::StatNameTagVectorOptConstRef tags_opt(setup.tags_);

  for (auto _ : state) {
    access_log_state.addInflightGauge(setup.stat_name_, tags_opt, Stats::Gauge::ImportMode::Accumulate, 1);
  }
}
BENCHMARK(bmAccessLogStateNewAdd);

static void bmAccessLogStateOldAdd(benchmark::State& state) {
  BencherSetup setup;
  Stats::IsolatedStoreImpl store(setup.symbol_table);
  AccessLogStateOldTest access_log_state(*store.rootScope());
  Stats::StatNameTagVectorOptConstRef tags_opt(setup.tags_);

  for (auto _ : state) {
    access_log_state.addInflightGauge(setup.stat_name_, tags_opt, Stats::Gauge::ImportMode::Accumulate, 1);
  }
}
BENCHMARK(bmAccessLogStateOldAdd);

static void bmAccessLogStateNewAddAndRemove(benchmark::State& state) {
  BencherSetup setup;
  Stats::IsolatedStoreImpl store(setup.symbol_table);
  AccessLogStateNewTest access_log_state(*store.rootScope());
  Stats::StatNameTagVectorOptConstRef tags_opt(setup.tags_);

  for (auto _ : state) {
    access_log_state.addInflightGauge(setup.stat_name_, tags_opt, Stats::Gauge::ImportMode::Accumulate, 1);
    access_log_state.removeInflightGauge(setup.stat_name_, tags_opt, Stats::Gauge::ImportMode::Accumulate, 1);
  }
}
BENCHMARK(bmAccessLogStateNewAddAndRemove);

static void bmAccessLogStateOldAddAndRemove(benchmark::State& state) {
  BencherSetup setup;
  Stats::IsolatedStoreImpl store(setup.symbol_table);
  AccessLogStateOldTest access_log_state(*store.rootScope());
  Stats::StatNameTagVectorOptConstRef tags_opt(setup.tags_);

  for (auto _ : state) {
    access_log_state.addInflightGauge(setup.stat_name_, tags_opt, Stats::Gauge::ImportMode::Accumulate, 1);
    access_log_state.removeInflightGauge(setup.stat_name_, tags_opt, Stats::Gauge::ImportMode::Accumulate, 1);
  }
}
BENCHMARK(bmAccessLogStateOldAddAndRemove);

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
