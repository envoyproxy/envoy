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
  Gauge& gaugeFromStatName(StatName name) override;
  Histogram& histogramFromStatName(StatName name) override;
  void deliverHistogramToSinks(const Histogram& histograms, uint64_t val) override;

  Counter& counter(const std::string& name) override {
    StatNameManagedStorage storage(name, symbolTable());
    return counterFromStatName(storage.statName());
  }
  Gauge& gauge(const std::string& name) override {
    StatNameManagedStorage storage(name, symbolTable());
    return gaugeFromStatName(storage.statName());
  }
  Histogram& histogram(const std::string& name) override {
    StatNameManagedStorage storage(name, symbolTable());
    return histogramFromStatName(storage.statName());
  }

  const Stats::StatsOptions& statsOptions() const override { return scope_.statsOptions(); }
  const SymbolTable& symbolTable() const override { return scope_.symbolTable(); }
  virtual SymbolTable& symbolTable() override { return scope_.symbolTable(); }

  NullGaugeImpl& nullGauge(const std::string& str) override { return scope_.nullGauge(str); }

private:
  Scope& scope_;
  StatNameStorage prefix_;
};

} // namespace Stats
} // namespace Envoy
