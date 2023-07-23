#include "source/common/stats/utility.h"

#include <algorithm>
#include <string>

#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

std::string Utility::sanitizeStatsName(absl::string_view name) {
  if (absl::EndsWith(name, ".")) {
    name.remove_suffix(1);
  }
  if (absl::StartsWith(name, ".")) {
    name.remove_prefix(1);
  }

  return absl::StrReplaceAll(name, {
                                       {"://", "_"},
                                       {":/", "_"},
                                       {":", "_"},
                                       {absl::string_view("\0", 1), "_"},
                                   });
}

absl::optional<StatName> Utility::findTag(const Metric& metric, StatName find_tag_name) {
  absl::optional<StatName> value;
  metric.iterateTagStatNames(
      [&value, &find_tag_name](Stats::StatName tag_name, Stats::StatName tag_value) -> bool {
        if (tag_name == find_tag_name) {
          value = tag_value;
          return false;
        }
        return true;
      });
  return value;
}

namespace {

// Helper class for the three Utility::*FromElements implementations to build up
// a joined StatName from a mix of StatName and string_view.
struct ElementVisitor {
  ElementVisitor(SymbolTable& symbol_table, const ElementVec& elements)
      : symbol_table_(symbol_table), pool_(symbol_table) {
    stat_names_.resize(elements.size());
    for (const Element& element : elements) {
      absl::visit(*this, element);
    }
    joined_ = symbol_table_.join(stat_names_);
  }

  // Overloads provides for absl::visit to call.
  void operator()(StatName stat_name) { stat_names_.push_back(stat_name); }
  void operator()(absl::string_view name) { stat_names_.push_back(pool_.add(name)); }

  /**
   * @return the StatName constructed by joining the elements.
   */
  StatName statName() { return StatName(joined_.get()); }

  SymbolTable& symbol_table_;
  StatNameVec stat_names_;
  StatNameDynamicPool pool_;
  SymbolTable::StoragePtr joined_;
};

} // namespace

namespace Utility {

ScopeSharedPtr scopeFromStatNames(Scope& scope, const StatNameVec& elements) {
  SymbolTable::StoragePtr joined = scope.symbolTable().join(elements);
  return scope.scopeFromStatName(StatName(joined.get()));
}

Counter& counterFromElements(Scope& scope, const ElementVec& elements,
                             StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(scope.symbolTable(), elements);
  return scope.counterFromStatNameWithTags(visitor.statName(), tags);
}

Counter& counterFromStatNames(Scope& scope, const StatNameVec& elements,
                              StatNameTagVectorOptConstRef tags) {
  SymbolTable::StoragePtr joined = scope.symbolTable().join(elements);
  return scope.counterFromStatNameWithTags(StatName(joined.get()), tags);
}

Gauge& gaugeFromElements(Scope& scope, const ElementVec& elements, Gauge::ImportMode import_mode,
                         StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(scope.symbolTable(), elements);
  return scope.gaugeFromStatNameWithTags(visitor.statName(), tags, import_mode);
}

Gauge& gaugeFromStatNames(Scope& scope, const StatNameVec& elements, Gauge::ImportMode import_mode,
                          StatNameTagVectorOptConstRef tags) {
  SymbolTable::StoragePtr joined = scope.symbolTable().join(elements);
  return scope.gaugeFromStatNameWithTags(StatName(joined.get()), tags, import_mode);
}

Histogram& histogramFromElements(Scope& scope, const ElementVec& elements, Histogram::Unit unit,
                                 StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(scope.symbolTable(), elements);
  return scope.histogramFromStatNameWithTags(visitor.statName(), tags, unit);
}

Histogram& histogramFromStatNames(Scope& scope, const StatNameVec& elements, Histogram::Unit unit,
                                  StatNameTagVectorOptConstRef tags) {
  SymbolTable::StoragePtr joined = scope.symbolTable().join(elements);
  return scope.histogramFromStatNameWithTags(StatName(joined.get()), tags, unit);
}

TextReadout& textReadoutFromElements(Scope& scope, const ElementVec& elements,
                                     StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(scope.symbolTable(), elements);
  return scope.textReadoutFromStatNameWithTags(visitor.statName(), tags);
}

TextReadout& textReadoutFromStatNames(Scope& scope, const StatNameVec& elements,
                                      StatNameTagVectorOptConstRef tags) {
  SymbolTable::StoragePtr joined = scope.symbolTable().join(elements);
  return scope.textReadoutFromStatNameWithTags(StatName(joined.get()), tags);
}

} // namespace Utility
} // namespace Stats
} // namespace Envoy
