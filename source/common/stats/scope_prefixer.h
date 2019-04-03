#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

// Implements a Scope that delegates to a passed-in scope, prefixing all names
// prior to creation.
class ScopePrefixer : public Scope {
public:
  // Variant of ScopePrefixer that owns the scope being prefixed, and will
  // delete it upon destruction.
  ScopePrefixer(absl::string_view prefix, Scope* scope);

  // Variant of ScopePrefixer that references the scope being prefixed, but does
  // not own it.
  ScopePrefixer(absl::string_view prefix, Scope& scope);

  virtual ~ScopePrefixer();

  // Scope
  ScopePtr createScope(const std::string& name) override;
  Counter& counter(const std::string& name) override { return scope_->counter(prefix_ + name); }
  Gauge& gauge(const std::string& name) override { return scope_->gauge(prefix_ + name); }
  Histogram& histogram(const std::string& name) override {
    return scope_->histogram(prefix_ + name);
  }
  void deliverHistogramToSinks(const Histogram& histograms, uint64_t val) override;
  Counter& counterFromStatName(StatName name) override;
  //    return counter(symbolTable().toString(name));
  //}
  Gauge& gaugeFromStatName(StatName name) override;
  // return gauge(symbolTable().toString(name));
  //}
  Histogram& histogramFromStatName(StatName name) override;
  // return histogram(symbolTable().toString(name));
  //}

  const Stats::StatsOptions& statsOptions() const override { return scope_->statsOptions(); }
  const SymbolTable& symbolTable() const override { return scope_->symbolTable(); }
  virtual SymbolTable& symbolTable() override { return scope_->symbolTable(); }

  NullGaugeImpl& nullGauge(const std::string& str) override { return scope_->nullGauge(str); }

private:
  std::string prefix_;
  Scope* scope_;
  const bool owns_scope_;
};

} // namespace Stats
} // namespace Envoy
