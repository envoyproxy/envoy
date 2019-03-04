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
  Counter& counterx(StatName name) override;
  Gauge& gaugex(StatName name) override;
  Histogram& histogramx(StatName name) override;
  void deliverHistogramToSinks(const Histogram& histograms, uint64_t val) override;

  Counter& counter(const std::string& name) override {
    StatNameTempStorage storage(name, symbolTable());
    return counterx(storage.statName());
  }
  Gauge& gauge(const std::string& name) override {
    StatNameTempStorage storage(name, symbolTable());
    return gaugex(storage.statName());
  }
  Histogram& histogram(const std::string& name) override {
    StatNameTempStorage storage(name, symbolTable());
    return histogramx(storage.statName());
  }

  const Stats::StatsOptions& statsOptions() const override { return scope_->statsOptions(); }
  const SymbolTable& symbolTable() const override { return scope_->symbolTable(); }
  virtual SymbolTable& symbolTable() override { return scope_->symbolTable(); }

private:
  StatNameStorage prefix_;
  Scope* scope_;
  const bool owns_scope_;
};

} // namespace Stats
} // namespace Envoy
