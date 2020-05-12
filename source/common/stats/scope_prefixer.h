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

private:
  Scope& scope_;
  StatNameStorage prefix_;
};

} // namespace Stats
} // namespace Envoy
