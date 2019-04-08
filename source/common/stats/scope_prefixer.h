#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

// Implements a Scope that delegates to a passed-in scope, prefixing all names
// prior to creation.
class ScopePrefixer : public Scope {
public:
  ScopePrefixer(absl::string_view prefix, Scope& scope);

  // Scope
  ScopePtr createScope(const std::string& name) override;
  Counter& counter(const std::string& name) override { return scope_.counter(prefix_ + name); }
  Gauge& gauge(const std::string& name) override { return scope_.gauge(prefix_ + name); }
  Histogram& histogram(const std::string& name) override {
    return scope_.histogram(prefix_ + name);
  }
  void deliverHistogramToSinks(const Histogram& histograms, uint64_t val) override;
  Counter& counterFromStatName(StatName name) override;
  Gauge& gaugeFromStatName(StatName name) override;
  Histogram& histogramFromStatName(StatName name) override;

  const Stats::StatsOptions& statsOptions() const override { return scope_.statsOptions(); }
  const SymbolTable& symbolTable() const override { return scope_.symbolTable(); }
  virtual SymbolTable& symbolTable() override { return scope_.symbolTable(); }

  NullGaugeImpl& nullGauge(const std::string& str) override { return scope_.nullGauge(str); }

private:
  std::string prefix_;
  Scope& scope_;
};

} // namespace Stats
} // namespace Envoy
