#include "common/stats/scope_prefixer.h"

#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"
#include "common/stats/utility.h"

namespace Envoy {
namespace Stats {

// Variant of ScopePrefixer that owns the scope being prefixed, and will
// delete it upon destruction.
ScopePrefixer::ScopePrefixer(absl::string_view prefix, Scope* scope)
    : prefix_(Utility::sanitizeStatsName(prefix), scope->symbolTable()), scope_(scope),
      owns_scope_(true) {}

// Variant of ScopePrefixer that references the scope being prefixed, but does
// not own it.
ScopePrefixer::ScopePrefixer(absl::string_view prefix, Scope& scope)
    : prefix_(Utility::sanitizeStatsName(prefix), scope.symbolTable()), scope_(&scope),
      owns_scope_(false) {}

ScopePrefixer::~ScopePrefixer() {
  prefix_.free(scope_->symbolTable());
  if (owns_scope_) {
    delete scope_;
  }
}

ScopePtr ScopePrefixer::createScope(const std::string& name) {
  // StatNameStorage scope_stat_name(name, symbolTable());  // Takes a lock.
  // StatNameStorage joiner(prefix_.statName(), scope_stat_name.statName());
  return std::make_unique<ScopePrefixer>(prefix_.statName().toString(symbolTable()) + "." + name,
                                         *scope_);
}

Counter& ScopePrefixer::counterx(StatName name) {
  StatNameJoiner joiner(prefix_.statName(), name);
  return scope_->counterx(joiner.statName());
}

Gauge& ScopePrefixer::gaugex(StatName name) {
  StatNameJoiner joiner(prefix_.statName(), name);
  return scope_->gaugex(joiner.statName());
}

Histogram& ScopePrefixer::histogramx(StatName name) {
  StatNameJoiner joiner(prefix_.statName(), name);
  return scope_->histogramx(joiner.statName());
}

void ScopePrefixer::deliverHistogramToSinks(const Histogram& histograms, uint64_t val) {
  scope_->deliverHistogramToSinks(histograms, val);
}

} // namespace Stats
} // namespace Envoy
