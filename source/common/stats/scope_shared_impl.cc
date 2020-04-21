#include "common/stats/scope_shared_impl.h"

namespace Envoy {
namespace Stats {

namespace {

// Helper class for the three Scope::*FromElements implementations to build up
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

Counter& ScopeSharedImpl::counterFromElementsHelper(const ElementVec& elements,
                                                    StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(symbolTable());
  return counterFromStatNameWithTags(visitor.makeStatName(elements), tags);
}

Gauge& ScopeSharedImpl::gaugeFromElementsHelper(
    const ElementVec& elements, Gauge::ImportMode import_mode, StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(symbolTable());
  return gaugeFromStatNameWithTags(visitor.makeStatName(elements), tags, import_mode);
}

Histogram& ScopeSharedImpl::histogramFromElementsHelper(
    const ElementVec& elements, Histogram::Unit unit, StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(symbolTable());
  return histogramFromStatNameWithTags(visitor.makeStatName(elements), tags, unit);
}

} // namespace Stats
} // namespace Envoy
