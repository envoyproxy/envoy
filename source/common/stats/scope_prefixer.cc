#include "common/stats/scope_prefixer.h"

#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"
#include "common/stats/utility.h"

namespace Envoy {
namespace Stats {

ScopePrefixer::ScopePrefixer(absl::string_view prefix, Scope& scope)
    : scope_(scope), prefix_(Utility::sanitizeStatsName(prefix), symbolTable()) {}

ScopePrefixer::ScopePrefixer(StatName prefix, Scope& scope)
    : scope_(scope), prefix_(prefix, symbolTable()) {}

ScopePrefixer::~ScopePrefixer() { prefix_.free(symbolTable()); }

ScopePtr ScopePrefixer::createScopeFromStatName(StatName name) {
  SymbolTable::StoragePtr joined = symbolTable().join({prefix_.statName(), name});
  return std::make_unique<ScopePrefixer>(StatName(joined.get()), scope_);
}

ScopePtr ScopePrefixer::createScope(const std::string& name) {
  StatNameManagedStorage stat_name_storage(Utility::sanitizeStatsName(name), symbolTable());
  return createScopeFromStatName(stat_name_storage.statName());
}

Counter& ScopePrefixer::counterFromStatName(StatName name) {
  Stats::SymbolTable::StoragePtr stat_name_storage =
      scope_.symbolTable().join({prefix_.statName(), name});
  return scope_.counterFromStatName(StatName(stat_name_storage.get()));
}

Gauge& ScopePrefixer::gaugeFromStatName(StatName name, Gauge::ImportMode import_mode) {
  Stats::SymbolTable::StoragePtr stat_name_storage =
      scope_.symbolTable().join({prefix_.statName(), name});
  return scope_.gaugeFromStatName(StatName(stat_name_storage.get()), import_mode);
}

Histogram& ScopePrefixer::histogramFromStatName(StatName name, Histogram::Unit unit) {
  Stats::SymbolTable::StoragePtr stat_name_storage =
      scope_.symbolTable().join({prefix_.statName(), name});
  return scope_.histogramFromStatName(StatName(stat_name_storage.get()), unit);
}

OptionalCounter ScopePrefixer::findCounter(StatName name) const { return scope_.findCounter(name); }

OptionalGauge ScopePrefixer::findGauge(StatName name) const { return scope_.findGauge(name); }

OptionalHistogram ScopePrefixer::findHistogram(StatName name) const {
  return scope_.findHistogram(name);
}

void ScopePrefixer::deliverHistogramToSinks(const Histogram& histograms, uint64_t val) {
  scope_.deliverHistogramToSinks(histograms, val);
}

} // namespace Stats
} // namespace Envoy
