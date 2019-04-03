#include "common/stats/scope_prefixer.h"

#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"
#include "common/stats/utility.h"

namespace Envoy {
namespace Stats {

// Variant of ScopePrefixer that owns the scope being prefixed, and will
// delete it upon destruction.
ScopePrefixer::ScopePrefixer(absl::string_view prefix, Scope* scope)
    : prefix_(Utility::sanitizeStatsName(prefix)), scope_(scope), owns_scope_(true) {}

// Variant of ScopePrefixer that references the scope being prefixed, but does
// not own it.
ScopePrefixer::ScopePrefixer(absl::string_view prefix, Scope& scope)
    : prefix_(Utility::sanitizeStatsName(prefix)), scope_(&scope), owns_scope_(false) {}

ScopePrefixer::~ScopePrefixer() {
  if (owns_scope_) {
    delete scope_;
  }
}

ScopePtr ScopePrefixer::createScope(const std::string& name) {
  // StatNameStorage scope_stat_name(name, symbolTable());  // Takes a lock.
  // StatNameStorage joiner(prefix_.statName(), scope_stat_name.statName());
  return std::make_unique<ScopePrefixer>(prefix_ + name, *scope_);
}

Counter& ScopePrefixer::counterFromStatName(StatName name) {
  return counter(symbolTable().toString(name));
}

Gauge& ScopePrefixer::gaugeFromStatName(StatName name) {
  return gauge(symbolTable().toString(name));
}

Histogram& ScopePrefixer::histogramFromStatName(StatName name) {
  return histogram(symbolTable().toString(name));
}

void ScopePrefixer::deliverHistogramToSinks(const Histogram& histograms, uint64_t val) {
  scope_->deliverHistogramToSinks(histograms, val);
}

} // namespace Stats
} // namespace Envoy
