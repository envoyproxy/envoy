#include "common/stats/utility.h"

#include <algorithm>
#include <string>

#include "absl/strings/match.h"
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
  std::string stats_name = std::string(name);
  std::replace(stats_name.begin(), stats_name.end(), ':', '_');
  std::replace(stats_name.begin(), stats_name.end(), '\0', '_');
  return stats_name;
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
  ElementVisitor(SymbolTable& symbol_table) : symbol_table_(symbol_table), pool_(symbol_table) {}

  // Overloads provides for absl::visit to call.
  void operator()(StatName stat_name) { stat_names_.push_back(stat_name); }
  void operator()(absl::string_view name) { stat_names_.push_back(pool_.add(name)); }

  // Generates a StatName from the elements.
  StatName makeStatName(const ElementVec& elements) {
    for (const Element& element : elements) {
      absl::visit(*this, element);
    }
    joined_ = symbol_table_.join(stat_names_);
    return StatName(joined_.get());
  }

  SymbolTable& symbol_table_;
  StatNameVec stat_names_;
  StatNameDynamicPool pool_;
  SymbolTable::StoragePtr joined_;
};

} // namespace

Counter& Utility::counterFromElements(Scope& scope, const ElementVec& elements,
                                      StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(scope.symbolTable());
  return scope.counterFromStatNameWithTags(visitor.makeStatName(elements), tags);
}

Gauge& Utility::gaugeFromElements(Scope& scope, const ElementVec& elements,
                                  Gauge::ImportMode import_mode,
                                  StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(scope.symbolTable());
  return scope.gaugeFromStatNameWithTags(visitor.makeStatName(elements), tags, import_mode);
}

Histogram& Utility::histogramFromElements(Scope& scope, const ElementVec& elements,
                                          Histogram::Unit unit, StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(scope.symbolTable());
  return scope.histogramFromStatNameWithTags(visitor.makeStatName(elements), tags, unit);
}

} // namespace Stats
} // namespace Envoy
