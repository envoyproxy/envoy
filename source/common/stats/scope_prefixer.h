#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

// Implements a Scope that delegates to a passed-in scope, prefixing all names
// prior to creation.
class ScopePrefixer : public Scope {
public:
  ScopePrefixer(absl::string_view prefix, Scope& scope);
  ScopePrefixer(StatName prefix, Scope& scope);
  ~ScopePrefixer() override;

  ScopePtr createScopeFromStatName(StatName name);

  // Scope
  ScopePtr createScope(const std::string& name) override;
  Counter& counterFromStatNameWithTags(const StatName& name,
                                       StatNameTagVectorOptConstRef tags) override;
  Gauge& gaugeFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                   Gauge::ImportMode import_mode) override;
  Histogram& histogramFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                           Histogram::Unit unit) override;
  TextReadout& textReadoutFromStatNameWithTags(const StatName& name,
                                               StatNameTagVectorOptConstRef tags) override;
  void deliverHistogramToSinks(const Histogram& histograms, uint64_t val) override;

  Counter& counterFromString(const std::string& name) override {
    StatNameManagedStorage storage(name, symbolTable());
    return Scope::counterFromStatName(storage.statName());
  }
  Gauge& gaugeFromString(const std::string& name, Gauge::ImportMode import_mode) override {
    StatNameManagedStorage storage(name, symbolTable());
    return Scope::gaugeFromStatName(storage.statName(), import_mode);
  }
  Histogram& histogramFromString(const std::string& name, Histogram::Unit unit) override {
    StatNameManagedStorage storage(name, symbolTable());
    return Scope::histogramFromStatName(storage.statName(), unit);
  }
  TextReadout& textReadoutFromString(const std::string& name) override {
    StatNameManagedStorage storage(name, symbolTable());
    return Scope::textReadoutFromStatName(storage.statName());
  }

  CounterOptConstRef findCounter(StatName name) const override;
  GaugeOptConstRef findGauge(StatName name) const override;
  HistogramOptConstRef findHistogram(StatName name) const override;
  TextReadoutOptConstRef findTextReadout(StatName name) const override;

  const SymbolTable& constSymbolTable() const override { return scope_.constSymbolTable(); }
  SymbolTable& symbolTable() override { return scope_.symbolTable(); }

  NullGaugeImpl& nullGauge(const std::string& str) override { return scope_.nullGauge(str); }

  bool iterate(const IterateFn<Counter>& fn) const override { return iterHelper(fn); }
  bool iterate(const IterateFn<Gauge>& fn) const override { return iterHelper(fn); }
  bool iterate(const IterateFn<Histogram>& fn) const override { return iterHelper(fn); }
  bool iterate(const IterateFn<TextReadout>& fn) const override { return iterHelper(fn); }

private:
  template <class StatType> bool iterHelper(const IterateFn<StatType>& fn) const {
    // We determine here what's in the scope by looking at name
    // prefixes. Strictly speaking this is not correct, as a stat name can be in
    // different scopes. But there is no data in `ScopePrefixer` to resurrect
    // actual membership of a stat in a scope, so we go by name matching. Note
    // that `ScopePrefixer` is not used in `ThreadLocalStore`, which has
    // accurate maps describing which stats are in which scopes.
    //
    // TODO(jmarantz): In the scope of this limited implementation, it would be
    // faster to match on the StatName prefix. This would be possible if
    // SymbolTable exposed a split() method.
    std::string prefix_str = scope_.symbolTable().toString(prefix_.statName());
    if (!prefix_str.empty() && !absl::EndsWith(prefix_str, ".")) {
      prefix_str += ".";
    }
    IterateFn<StatType> filter_scope = [&fn,
                                        &prefix_str](const RefcountPtr<StatType>& stat) -> bool {
      return !absl::StartsWith(stat->name(), prefix_str) || fn(stat);
    };
    return scope_.iterate(filter_scope);
  }

  Scope& scope_;
  StatNameStorage prefix_;
};

} // namespace Stats
} // namespace Envoy
