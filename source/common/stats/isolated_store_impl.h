#pragma once

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>

#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"

#include "source/common/common/utility.h"
#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/null_counter.h"
#include "source/common/stats/null_gauge.h"
#include "source/common/stats/store_impl.h"
#include "source/common/stats/symbol_table_impl.h"
#include "source/common/stats/tag_utility.h"
#include "source/common/stats/utility.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Stats {

/**
 * A stats cache template that is used by the isolated store.
 */
template <class Base> class IsolatedStatsCache {
public:
  using CounterAllocator = std::function<RefcountPtr<Base>(StatName name)>;
  using GaugeAllocator = std::function<RefcountPtr<Base>(StatName, Gauge::ImportMode)>;
  using HistogramAllocator = std::function<RefcountPtr<Base>(StatName, Histogram::Unit)>;
  using TextReadoutAllocator = std::function<RefcountPtr<Base>(StatName name, TextReadout::Type)>;
  using BaseOptConstRef = absl::optional<std::reference_wrapper<const Base>>;

  IsolatedStatsCache(CounterAllocator alloc) : counter_alloc_(alloc) {}
  IsolatedStatsCache(GaugeAllocator alloc) : gauge_alloc_(alloc) {}
  IsolatedStatsCache(HistogramAllocator alloc) : histogram_alloc_(alloc) {}
  IsolatedStatsCache(TextReadoutAllocator alloc) : text_readout_alloc_(alloc) {}

  Base& get(StatName name) {
    auto stat = stats_.find(name);
    if (stat != stats_.end()) {
      return *stat->second;
    }

    RefcountPtr<Base> new_stat = counter_alloc_(name);
    stats_.emplace(new_stat->statName(), new_stat);
    if (sink_filter_ && sink_filter_(*new_stat)) {
      sinked_stats_.insert(new_stat.get());
    }
    return *new_stat;
  }

  Base& get(StatName name, Gauge::ImportMode import_mode) {
    auto stat = stats_.find(name);
    if (stat != stats_.end()) {
      return *stat->second;
    }

    RefcountPtr<Base> new_stat = gauge_alloc_(name, import_mode);
    stats_.emplace(new_stat->statName(), new_stat);
    if (sink_filter_ && sink_filter_(*new_stat)) {
      sinked_stats_.insert(new_stat.get());
    }
    return *new_stat;
  }

  Base& get(StatName name, Histogram::Unit unit) {
    auto stat = stats_.find(name);
    if (stat != stats_.end()) {
      return *stat->second;
    }

    RefcountPtr<Base> new_stat = histogram_alloc_(name, unit);
    stats_.emplace(new_stat->statName(), new_stat);
    return *new_stat;
  }

  Base& get(StatName name, TextReadout::Type type) {
    auto stat = stats_.find(name);
    if (stat != stats_.end()) {
      return *stat->second;
    }

    RefcountPtr<Base> new_stat = text_readout_alloc_(name, type);
    stats_.emplace(new_stat->statName(), new_stat);
    if (sink_filter_ && sink_filter_(*new_stat)) {
      sinked_stats_.insert(new_stat.get());
    }
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

  bool iterate(const IterateFn<Base>& fn) const {
    for (auto& stat : stats_) {
      if (!fn(stat.second)) {
        return false;
      }
    }
    return true;
  }

  void forEachSinkedStat(std::function<void(std::size_t)> f_size,
                         std::function<void(Base&)> f_stat) {
    if (sink_filter_ != nullptr) {
      f_size(sinked_stats_.size());
      for (auto& stat : sinked_stats_) {
        f_stat(*stat);
      }
    } else {
      f_size(stats_.size());
      for (auto const& stat : stats_) {
        f_stat(*stat.second);
      }
    }
  }

  void setSinkFilter(std::function<bool(const Base&)> filter) { sink_filter_ = filter; }

private:
  friend class IsolatedStoreImpl;

  BaseOptConstRef find(StatName name) const {
    auto stat = stats_.find(name);
    if (stat == stats_.end()) {
      return absl::nullopt;
    }
    return std::cref(*stat->second);
  }

  StatNameHashMap<RefcountPtr<Base>> stats_;
  absl::flat_hash_set<Base*, absl::Hash<Base*>> sinked_stats_;
  CounterAllocator counter_alloc_;
  GaugeAllocator gauge_alloc_;
  HistogramAllocator histogram_alloc_;
  TextReadoutAllocator text_readout_alloc_;
  std::function<bool(const Base&)> sink_filter_;
};

class IsolatedStoreImpl : public StoreImpl {
public:
  IsolatedStoreImpl();
  explicit IsolatedStoreImpl(SymbolTable& symbol_table);

  // Stats::Scope
  Counter& counterFromStatNameWithTags(const StatName& name,
                                       StatNameTagVectorOptConstRef tags) override {
    TagUtility::TagStatNameJoiner joiner(name, tags, symbolTable());
    Counter& counter = counters_.get(joiner.nameWithTags());
    return counter;
  }
  ScopePtr createScope(const std::string& name) override;
  ScopePtr scopeFromStatName(StatName name) override;
  void deliverHistogramToSinks(const Histogram&, uint64_t) override {}
  Gauge& gaugeFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                   Gauge::ImportMode import_mode) override {
    TagUtility::TagStatNameJoiner joiner(name, tags, symbolTable());
    Gauge& gauge = gauges_.get(joiner.nameWithTags(), import_mode);
    gauge.mergeImportMode(import_mode);
    return gauge;
  }
  NullCounterImpl& nullCounter() { return *null_counter_; }
  NullGaugeImpl& nullGauge(const std::string&) override { return *null_gauge_; }
  Histogram& histogramFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                           Histogram::Unit unit) override {
    TagUtility::TagStatNameJoiner joiner(name, tags, symbolTable());
    Histogram& histogram = histograms_.get(joiner.nameWithTags(), unit);
    return histogram;
  }
  TextReadout& textReadoutFromStatNameWithTags(const StatName& name,
                                               StatNameTagVectorOptConstRef tags) override {
    TagUtility::TagStatNameJoiner joiner(name, tags, symbolTable());
    TextReadout& text_readout =
        text_readouts_.get(joiner.nameWithTags(), TextReadout::Type::Default);
    return text_readout;
  }
  CounterOptConstRef findCounter(StatName name) const override { return counters_.find(name); }
  GaugeOptConstRef findGauge(StatName name) const override { return gauges_.find(name); }
  HistogramOptConstRef findHistogram(StatName name) const override {
    return histograms_.find(name);
  }
  TextReadoutOptConstRef findTextReadout(StatName name) const override {
    return text_readouts_.find(name);
  }

  bool iterate(const IterateFn<Counter>& fn) const override { return counters_.iterate(fn); }
  bool iterate(const IterateFn<Gauge>& fn) const override { return gauges_.iterate(fn); }
  bool iterate(const IterateFn<Histogram>& fn) const override { return histograms_.iterate(fn); }
  bool iterate(const IterateFn<TextReadout>& fn) const override {
    return text_readouts_.iterate(fn);
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
  std::vector<TextReadoutSharedPtr> textReadouts() const override {
    return text_readouts_.toVector();
  }

  Counter& counterFromString(const std::string& name) override {
    StatNameManagedStorage storage(name, symbolTable());
    return counterFromStatName(storage.statName());
  }
  Gauge& gaugeFromString(const std::string& name, Gauge::ImportMode import_mode) override {
    StatNameManagedStorage storage(name, symbolTable());
    return gaugeFromStatName(storage.statName(), import_mode);
  }
  Histogram& histogramFromString(const std::string& name, Histogram::Unit unit) override {
    StatNameManagedStorage storage(name, symbolTable());
    return histogramFromStatName(storage.statName(), unit);
  }
  TextReadout& textReadoutFromString(const std::string& name) override {
    StatNameManagedStorage storage(name, symbolTable());
    return textReadoutFromStatName(storage.statName());
  }

  void forEachSinkedCounter(std::function<void(std::size_t)> f_size,
                            std::function<void(Stats::Counter&)> f_stat) override {
    counters_.forEachSinkedStat(f_size, f_stat);
  }

  void forEachSinkedGauge(std::function<void(std::size_t)> f_size,
                          std::function<void(Stats::Gauge&)> f_stat) override {
    gauges_.forEachSinkedStat(f_size, f_stat);
  }

  void forEachSinkedTextReadout(std::function<void(std::size_t)> f_size,
                                std::function<void(Stats::TextReadout&)> f_stat) override {
    text_readouts_.forEachSinkedStat(f_size, f_stat);
  }

  void setCounterSinkFilter(std::function<bool(const Stats::Counter&)> filter) override {
    counters_.setSinkFilter(filter);
  }

  void setGaugeSinkFilter(std::function<bool(const Stats::Gauge&)> filter) override {
    gauges_.setSinkFilter(filter);
  }

  void setTextReadoutSinkFilter(std::function<bool(const Stats::TextReadout&)> filter) override {
    text_readouts_.setSinkFilter(filter);
  }

private:
  IsolatedStoreImpl(std::unique_ptr<SymbolTable>&& symbol_table);

  SymbolTablePtr symbol_table_storage_;
  AllocatorImpl alloc_;
  IsolatedStatsCache<Counter> counters_;
  IsolatedStatsCache<Gauge> gauges_;
  IsolatedStatsCache<Histogram> histograms_;
  IsolatedStatsCache<TextReadout> text_readouts_;
  RefcountPtr<NullCounterImpl> null_counter_;
  RefcountPtr<NullGaugeImpl> null_gauge_;
};

} // namespace Stats
} // namespace Envoy
