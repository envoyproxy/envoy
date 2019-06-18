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
  Counter& counterFromStatName(StatName name) override;
  Gauge& gaugeFromStatName(StatName name, Gauge::ImportMode import_mode) override;
  Histogram& histogramFromStatName(StatName name) override;
  void deliverHistogramToSinks(const Histogram& histograms, uint64_t val) override;

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

  absl::optional<std::reference_wrapper<const Counter>> findCounter(StatName name) const override;
  absl::optional<std::reference_wrapper<const Gauge>> findGauge(StatName name) const override;
  absl::optional<std::reference_wrapper<const Histogram>>
  findHistogram(StatName name) const override;

  const SymbolTable& constSymbolTable() const override { return scope_.constSymbolTable(); }
  virtual SymbolTable& symbolTable() override { return scope_.symbolTable(); }

  NullGaugeImpl& nullGauge(const std::string& str) override { return scope_.nullGauge(str); }

private:
  Scope& scope_;
  StatNameStorage prefix_;
};

} // namespace Stats
} // namespace Envoy
