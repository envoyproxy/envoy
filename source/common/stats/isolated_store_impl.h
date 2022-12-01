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
#include "source/common/stats/symbol_table.h"
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
    return *new_stat;
  }

  Base& get(StatName name, Gauge::ImportMode import_mode) {
    auto stat = stats_.find(name);
    if (stat != stats_.end()) {
      return *stat->second;
    }

    RefcountPtr<Base> new_stat = gauge_alloc_(name, import_mode);
    stats_.emplace(new_stat->statName(), new_stat);
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

  void forEachStat(SizeFn f_size, StatFn<Base> f_stat) const {
    if (f_size != nullptr) {
      f_size(stats_.size());
    }
    for (auto const& stat : stats_) {
      f_stat(*stat.second);
    }
  }

  BaseOptConstRef find(StatName name) const {
    auto stat = stats_.find(name);
    if (stat == stats_.end()) {
      return absl::nullopt;
    }
    return std::cref(*stat->second);
  }

private:
  StatNameHashMap<RefcountPtr<Base>> stats_;
  CounterAllocator counter_alloc_;
  GaugeAllocator gauge_alloc_;
  HistogramAllocator histogram_alloc_;
  TextReadoutAllocator text_readout_alloc_;
};

// Isolated implementation of Stats::Store. This class is not thread-safe by
// itself, but a thread-safe wrapper can be built, e.g. TestIsolatedStoreImpl
// in test/integration/server.h.
class IsolatedStoreImpl : public Store {
public:
  IsolatedStoreImpl();
  explicit IsolatedStoreImpl(SymbolTable& symbol_table);
  ~IsolatedStoreImpl() override;

  // Stats::Store
  const SymbolTable& constSymbolTable() const override { return alloc_.constSymbolTable(); }
  SymbolTable& symbolTable() override { return alloc_.symbolTable(); }

  void deliverHistogramToSinks(const Histogram&, uint64_t) override {}
  ScopeSharedPtr rootScope() override;
  ConstScopeSharedPtr constRootScope() const override;
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

  void forEachCounter(SizeFn f_size, StatFn<Counter> f_stat) const override {
    counters_.forEachStat(f_size, f_stat);
  }

  void forEachGauge(SizeFn f_size, StatFn<Gauge> f_stat) const override {
    gauges_.forEachStat(f_size, f_stat);
  }

  void forEachTextReadout(SizeFn f_size, StatFn<TextReadout> f_stat) const override {
    text_readouts_.forEachStat(f_size, f_stat);
  }

  void forEachHistogram(SizeFn f_size, StatFn<ParentHistogram> f_stat) const override {
    UNREFERENCED_PARAMETER(f_size);
    UNREFERENCED_PARAMETER(f_stat);
  }

  void forEachScope(SizeFn f_size, StatFn<const Scope> f_stat) const override {
    if (f_size != nullptr) {
      f_size(scopes_.size() + 1);
    }
    f_stat(*constRootScope());
    for (const ScopeSharedPtr& scope : scopes_) {
      f_stat(*scope);
    }
  }

  void forEachSinkedCounter(SizeFn f_size, StatFn<Counter> f_stat) const override {
    forEachCounter(f_size, f_stat);
  }

  void forEachSinkedGauge(SizeFn f_size, StatFn<Gauge> f_stat) const override {
    forEachGauge(f_size, f_stat);
  }

  void forEachSinkedTextReadout(SizeFn f_size, StatFn<TextReadout> f_stat) const override {
    forEachTextReadout(f_size, f_stat);
  }

  CounterOptConstRef findCounter(StatName name) const override { return counters_.find(name); }
  GaugeOptConstRef findGauge(StatName name) const override { return gauges_.find(name); }
  HistogramOptConstRef findHistogram(StatName name) const override {
    return histograms_.find(name);
  }
  TextReadoutOptConstRef findTextReadout(StatName name) const override {
    return text_readouts_.find(name);
  }

  NullCounterImpl& nullCounter() override { return *null_counter_; }
  NullGaugeImpl& nullGauge() override { return *null_gauge_; }

  bool iterate(const IterateFn<Counter>& fn) const override {
    return constRootScope()->iterate(fn);
  }
  bool iterate(const IterateFn<Gauge>& fn) const override { return constRootScope()->iterate(fn); }
  bool iterate(const IterateFn<Histogram>& fn) const override {
    return constRootScope()->iterate(fn);
  }
  bool iterate(const IterateFn<TextReadout>& fn) const override {
    return constRootScope()->iterate(fn);
  }

protected:
  /**
   * Provides a hook for sub-classes to define how to create new scopes. When
   * subclassing IsolatedStoreImpl you likely want to also subclass
   * IsolatedScopeImpl. Overriding this method enables scopes of the appropriate
   * type to be created from Store::rootScope(), Scope::scopeFromStatName, and
   * Scope::createScope, without needing to override those. makeScope is usually
   * implemented by "return std::make_shared<YourScopeType>(name, *this)".
   *
   * @param name the fully qualified stat name -- no further prefixing needed.
   */
  virtual ScopeSharedPtr makeScope(StatName name);

private:
  friend class IsolatedScopeImpl;

  IsolatedStoreImpl(std::unique_ptr<SymbolTable>&& symbol_table);

  SymbolTablePtr symbol_table_storage_;
  AllocatorImpl alloc_;
  IsolatedStatsCache<Counter> counters_;
  IsolatedStatsCache<Gauge> gauges_;
  IsolatedStatsCache<Histogram> histograms_;
  IsolatedStatsCache<TextReadout> text_readouts_;
  RefcountPtr<NullCounterImpl> null_counter_;
  RefcountPtr<NullGaugeImpl> null_gauge_;
  mutable ScopeSharedPtr lazy_default_scope_;
  std::vector<ScopeSharedPtr> scopes_;
};

class IsolatedScopeImpl : public Scope {
public:
  IsolatedScopeImpl(const std::string& prefix, IsolatedStoreImpl& store)
      : prefix_(prefix, store.symbolTable()), store_(store) {}

  IsolatedScopeImpl(StatName prefix, IsolatedStoreImpl& store)
      : prefix_(prefix, store.symbolTable()), store_(store) {}

  ~IsolatedScopeImpl() override { prefix_.free(store_.symbolTable()); }

  // Stats::Scope
  SymbolTable& symbolTable() override { return store_.symbolTable(); }
  const SymbolTable& constSymbolTable() const override { return store_.symbolTable(); }
  Counter& counterFromStatNameWithTags(const StatName& name,
                                       StatNameTagVectorOptConstRef tags) override {
    TagUtility::TagStatNameJoiner joiner(prefix(), name, tags, symbolTable());
    Counter& counter = store_.counters_.get(joiner.nameWithTags());
    return counter;
  }
  ScopeSharedPtr createScope(const std::string& name) override;
  ScopeSharedPtr scopeFromStatName(StatName name) override;
  Gauge& gaugeFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                   Gauge::ImportMode import_mode) override {
    TagUtility::TagStatNameJoiner joiner(prefix(), name, tags, symbolTable());
    Gauge& gauge = store_.gauges_.get(joiner.nameWithTags(), import_mode);
    gauge.mergeImportMode(import_mode);
    return gauge;
  }
  Histogram& histogramFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                           Histogram::Unit unit) override {
    TagUtility::TagStatNameJoiner joiner(prefix(), name, tags, symbolTable());
    Histogram& histogram = store_.histograms_.get(joiner.nameWithTags(), unit);
    return histogram;
  }
  TextReadout& textReadoutFromStatNameWithTags(const StatName& name,
                                               StatNameTagVectorOptConstRef tags) override {
    TagUtility::TagStatNameJoiner joiner(prefix(), name, tags, symbolTable());
    TextReadout& text_readout =
        store_.text_readouts_.get(joiner.nameWithTags(), TextReadout::Type::Default);
    return text_readout;
  }
  CounterOptConstRef findCounter(StatName name) const override {
    return store_.counters_.find(name);
  }
  GaugeOptConstRef findGauge(StatName name) const override { return store_.gauges_.find(name); }
  HistogramOptConstRef findHistogram(StatName name) const override {
    return store_.histograms_.find(name);
  }
  TextReadoutOptConstRef findTextReadout(StatName name) const override {
    return store_.text_readouts_.find(name);
  }

  bool iterate(const IterateFn<Counter>& fn) const override {
    return store_.counters_.iterate(iterFilter(fn));
  }
  bool iterate(const IterateFn<Gauge>& fn) const override {
    return store_.gauges_.iterate(iterFilter(fn));
  }
  bool iterate(const IterateFn<Histogram>& fn) const override {
    return store_.histograms_.iterate(iterFilter(fn));
  }
  bool iterate(const IterateFn<TextReadout>& fn) const override {
    return store_.text_readouts_.iterate(iterFilter(fn));
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

  StatName prefix() const override { return prefix_.statName(); }
  IsolatedStoreImpl& store() override { return store_; }
  const IsolatedStoreImpl& constStore() const override { return store_; }

protected:
  void addScopeToStore(const ScopeSharedPtr& scope) { store_.scopes_.push_back(scope); }

private:
  template <class StatType> IterateFn<StatType> iterFilter(const IterateFn<StatType>& fn) const {
    // We determine here what's in the scope by looking at name
    // prefixes. Strictly speaking this is not correct, as the same stat can be
    // in different scopes, e.g. counter "b.c" in scope "a", and counter "c"
    // created in scope "a.b".
    //
    // There is currently no mechanism to resurrect actual membership of a stat
    // in a scope, so we go by name matching. Note that this hack is not needed
    // in `ThreadLocalStore`, which has accurate maps describing which stats are
    // in which scopes.
    //
    // TODO(jmarantz): In the scope of this limited implementation, it would be
    // faster to match on the StatName prefix. This would be possible if
    // SymbolTable exposed a split() method.
    std::string prefix_str = constSymbolTable().toString(prefix_.statName());
    if (!prefix_str.empty() && !absl::EndsWith(prefix_str, ".")) {
      prefix_str += ".";
    }
    return [fn, prefix_str](const RefcountPtr<StatType>& stat) -> bool {
      return !absl::StartsWith(stat->name(), prefix_str) || fn(stat);
    };
  }

  StatNameStorage prefix_;
  IsolatedStoreImpl& store_;
};

} // namespace Stats
} // namespace Envoy
