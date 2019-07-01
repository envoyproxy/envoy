#pragma once

#include <string.h>

#include <algorithm>
#include <string>

#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"

#include "common/common/utility.h"
#include "common/stats/heap_stat_data.h"
#include "common/stats/null_counter.h"
#include "common/stats/null_gauge.h"
#include "common/stats/store_impl.h"
#include "common/stats/symbol_table_impl.h"
#include "common/stats/utility.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Stats {

/**
 * A stats cache template that is used by the isolated store.
 */
template <class Base> class IsolatedStatsCache {
public:
  using Allocator = std::function<RefcountPtr<Base>(StatName name)>;
  using AllocatorImportMode = std::function<RefcountPtr<Base>(StatName, Gauge::ImportMode)>;

  IsolatedStatsCache(Allocator alloc) : alloc_(alloc) {}
  IsolatedStatsCache(AllocatorImportMode alloc) : alloc_import_(alloc) {}

  Base& get(StatName name) {
    auto stat = stats_.find(name);
    if (stat != stats_.end()) {
      return *stat->second;
    }

    RefcountPtr<Base> new_stat = alloc_(name);
    stats_.emplace(new_stat->statName(), new_stat);
    return *new_stat;
  }

  Base& get(StatName name, Gauge::ImportMode import_mode) {
    auto stat = stats_.find(name);
    if (stat != stats_.end()) {
      return *stat->second;
    }

    RefcountPtr<Base> new_stat = alloc_import_(name, import_mode);
    stats_.emplace(new_stat->statName(), new_stat);
    return *new_stat;
  }

  std::vector<RefcountPtr<Base>> toVector() const {
    std::vector<RefcountPtr<Base>> vec;
    vec.reserve(stats_.size());
    for (auto& stat : stats_) {
      vec.push_back(stat.second);
    }

    return vec;
  }

private:
  friend class IsolatedStoreImpl;

  absl::optional<std::reference_wrapper<const Base>> find(StatName name) const {
    auto stat = stats_.find(name);
    if (stat == stats_.end()) {
      return absl::nullopt;
    }
    return std::cref(*stat->second);
  }

  StatNameHashMap<RefcountPtr<Base>> stats_;
  Allocator alloc_;
  AllocatorImportMode alloc_import_;
};

class IsolatedStoreImpl : public StoreImpl {
public:
  IsolatedStoreImpl();
  explicit IsolatedStoreImpl(SymbolTable& symbol_table);

  // Stats::Scope
  Counter& counterFromStatName(StatName name) override { return counters_.get(name); }
  ScopePtr createScope(const std::string& name) override;
  void deliverHistogramToSinks(const Histogram&, uint64_t) override {}
  Gauge& gaugeFromStatName(StatName name, Gauge::ImportMode import_mode) override {
    Gauge& gauge = gauges_.get(name, import_mode);
    gauge.mergeImportMode(import_mode);
    return gauge;
  }
  NullCounterImpl& nullCounter() { return *null_counter_; }
  NullGaugeImpl& nullGauge(const std::string&) override { return *null_gauge_; }
  Histogram& histogramFromStatName(StatName name) override {
    Histogram& histogram = histograms_.get(name);
    return histogram;
  }
  absl::optional<std::reference_wrapper<const Counter>> findCounter(StatName name) const override {
    return counters_.find(name);
  }
  absl::optional<std::reference_wrapper<const Gauge>> findGauge(StatName name) const override {
    return gauges_.find(name);
  }
  absl::optional<std::reference_wrapper<const Histogram>>
  findHistogram(StatName name) const override {
    return histograms_.find(name);
  }

  // Stats::Store
  std::vector<CounterSharedPtr> counters() const override { return counters_.toVector(); }
  std::vector<GaugeSharedPtr> gauges() const override {
    // TODO(jmarantz): should we filter out gauges where
    // gauge.importMode() != Gauge::ImportMode::Uninitialized ?
    // I don't think this matters because that should only occur for gauges
    // received in a hot-restart transfer, and isolated-store gauges should
    // never be transmitted that way.
    return gauges_.toVector();
  }
  std::vector<ParentHistogramSharedPtr> histograms() const override {
    return std::vector<ParentHistogramSharedPtr>{};
  }

  Counter& counter(const std::string& name) override {
    StatNameManagedStorage storage(name, symbolTable());
    return counterFromStatName(storage.statName());
  }
  Gauge& gauge(const std::string& name, Gauge::ImportMode import_mode) override {
    StatNameManagedStorage storage(name, symbolTable());
    return gaugeFromStatName(storage.statName(), import_mode);
  }
  Histogram& histogram(const std::string& name) override {
    StatNameManagedStorage storage(name, symbolTable());
    return histogramFromStatName(storage.statName());
  }

private:
  IsolatedStoreImpl(std::unique_ptr<SymbolTable>&& symbol_table);

  std::unique_ptr<SymbolTable> symbol_table_storage_;
  HeapStatDataAllocator alloc_;
  IsolatedStatsCache<Counter> counters_;
  IsolatedStatsCache<Gauge> gauges_;
  IsolatedStatsCache<Histogram> histograms_;
  RefcountPtr<NullCounterImpl> null_counter_;
  RefcountPtr<NullGaugeImpl> null_gauge_;
};

} // namespace Stats
} // namespace Envoy
