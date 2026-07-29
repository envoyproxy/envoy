#include "source/common/stats/utility.h"

#include <algorithm>
#include <optional>
#include <string>

#include "source/common/stats/symbol_table.h"

#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"

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

absl::string_view Utility::sanitizeStatsName(absl::string_view name, std::string& buffer) {
  name = absl::StripPrefix(absl::StripSuffix(name, "."), ".");

  // Check if the name needs sanitization.
  if (name.find(':') != absl::string_view::npos || name.find('\0') != absl::string_view::npos) {
    buffer = absl::StrReplaceAll(name, {
                                           {"://", "_"},
                                           {":/", "_"},
                                           {":", "_"},
                                           {absl::string_view("\0", 1), "_"},
                                       });
    return buffer;
  }
  return name;
}

TaggedStatName::TaggedStatName(SymbolTable& symbol_table, absl::string_view base_name,
                               TagStringViewSpan tags, absl::string_view name)
    : tag_pool_(symbol_table) {
  tag_pool_.reserve(tags.size() * 2 + 2);

  std::string buffer;
  base_name_ = tag_pool_.add(Utility::sanitizeStatsName(base_name, buffer));
  if (tags.empty()) {
    name_ = base_name_;
  } else {
    ASSERT(!name.empty(), "When tags are supplied, the caller must supply the tagged name with the "
                          "tag values interleaved.");
    name_ = tag_pool_.add(Utility::sanitizeStatsName(name, buffer));
  }

  tags_.reserve(tags.size());
  for (const TagStringView& tag : tags) {
    tags_.push_back({tag_pool_.add(Utility::sanitizeStatsName(tag.first, buffer)),
                     tag_pool_.add(Utility::sanitizeStatsName(tag.second, buffer))});
  }
}

std::optional<StatName> Utility::findTag(const Metric& metric, StatName find_tag_name) {
  std::optional<StatName> value;
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
  return scope.counterFromTaggedName(visitor.statName(), Scope::toTagSpan(tags), StatName());
}

Counter& counterFromStatNames(Scope& scope, const StatNameVec& elements,
                              StatNameTagVectorOptConstRef tags) {
  SymbolTable::StoragePtr joined = scope.symbolTable().join(elements);
  return scope.counterFromTaggedName(StatName(joined.get()), Scope::toTagSpan(tags), StatName());
}

Gauge& gaugeFromElements(Scope& scope, const ElementVec& elements, Gauge::ImportMode import_mode,
                         StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(scope.symbolTable(), elements);
  return scope.gaugeFromTaggedName(visitor.statName(), Scope::toTagSpan(tags), StatName(),
                                   import_mode);
}

Gauge& gaugeFromStatNames(Scope& scope, const StatNameVec& elements, Gauge::ImportMode import_mode,
                          StatNameTagVectorOptConstRef tags) {
  SymbolTable::StoragePtr joined = scope.symbolTable().join(elements);
  return scope.gaugeFromTaggedName(StatName(joined.get()), Scope::toTagSpan(tags), StatName(),
                                   import_mode);
}

Histogram& histogramFromElements(Scope& scope, const ElementVec& elements, Histogram::Unit unit,
                                 StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(scope.symbolTable(), elements);
  return scope.histogramFromTaggedName(visitor.statName(), Scope::toTagSpan(tags), StatName(),
                                       unit);
}

Histogram& histogramFromStatNames(Scope& scope, const StatNameVec& elements, Histogram::Unit unit,
                                  StatNameTagVectorOptConstRef tags) {
  SymbolTable::StoragePtr joined = scope.symbolTable().join(elements);
  return scope.histogramFromTaggedName(StatName(joined.get()), Scope::toTagSpan(tags), StatName(),
                                       unit);
}

TextReadout& textReadoutFromElements(Scope& scope, const ElementVec& elements,
                                     StatNameTagVectorOptConstRef tags) {
  ElementVisitor visitor(scope.symbolTable(), elements);
  return scope.textReadoutFromTaggedName(visitor.statName(), Scope::toTagSpan(tags), StatName());
}

TextReadout& textReadoutFromStatNames(Scope& scope, const StatNameVec& elements,
                                      StatNameTagVectorOptConstRef tags) {
  SymbolTable::StoragePtr joined = scope.symbolTable().join(elements);
  return scope.textReadoutFromTaggedName(StatName(joined.get()), Scope::toTagSpan(tags),
                                         StatName());
}

Counter& counterFromTaggedPrefix(Scope& scope, StatName base_prefix, StatNameTagSpan prefix_tags,
                                 StatName prefix, absl::string_view name) {
  StatNameManagedStorage name_storage(name, scope.symbolTable());
  return counterFromTaggedPrefix(scope, base_prefix, prefix_tags, prefix, name_storage.statName());
}

Gauge& gaugeFromTaggedPrefix(Scope& scope, StatName base_prefix, StatNameTagSpan prefix_tags,
                             StatName prefix, absl::string_view name,
                             Gauge::ImportMode import_mode) {
  StatNameManagedStorage name_storage(name, scope.symbolTable());
  return gaugeFromTaggedPrefix(scope, base_prefix, prefix_tags, prefix, name_storage.statName(),
                               import_mode);
}

Histogram& histogramFromTaggedPrefix(Scope& scope, StatName base_prefix,
                                     StatNameTagSpan prefix_tags, StatName prefix,
                                     absl::string_view name, Histogram::Unit unit) {
  StatNameManagedStorage name_storage(name, scope.symbolTable());
  return histogramFromTaggedPrefix(scope, base_prefix, prefix_tags, prefix, name_storage.statName(),
                                   unit);
}

TextReadout& textReadoutFromTaggedPrefix(Scope& scope, StatName base_prefix,
                                         StatNameTagSpan prefix_tags, StatName prefix,
                                         absl::string_view name) {
  StatNameManagedStorage name_storage(name, scope.symbolTable());
  return textReadoutFromTaggedPrefix(scope, base_prefix, prefix_tags, prefix,
                                     name_storage.statName());
}

Counter& counterFromTaggedPrefix(Scope& scope, StatName base_prefix, StatNameTagSpan prefix_tags,
                                 StatName prefix, StatName name) {
  SymbolTable& symbol_table = scope.symbolTable();
  if (prefix_tags.empty()) {
    prefix = base_prefix;
  } else {
    ASSERT(!prefix.empty(),
           "When tags are supplied, the caller must supply the tagged prefix with the "
           "tag values interleaved.");
  }

  SymbolTable::StoragePtr full_name = symbol_table.join({prefix, name});
  SymbolTable::StoragePtr full_name_base = symbol_table.join({base_prefix, name});
  return scope.counterFromTaggedName(StatName(full_name_base.get()), prefix_tags,
                                     StatName(full_name.get()));
}

Gauge& gaugeFromTaggedPrefix(Scope& scope, StatName base_prefix, StatNameTagSpan prefix_tags,
                             StatName prefix, StatName name, Gauge::ImportMode import_mode) {
  SymbolTable& symbol_table = scope.symbolTable();
  if (prefix_tags.empty()) {
    prefix = base_prefix;
  } else {
    ASSERT(!prefix.empty(),
           "When tags are supplied, the caller must supply the tagged prefix with the "
           "tag values interleaved.");
  }

  SymbolTable::StoragePtr full_name = symbol_table.join({prefix, name});
  SymbolTable::StoragePtr full_name_base = symbol_table.join({base_prefix, name});
  return scope.gaugeFromTaggedName(StatName(full_name_base.get()), prefix_tags,
                                   StatName(full_name.get()), import_mode);
}

Histogram& histogramFromTaggedPrefix(Scope& scope, StatName base_prefix,
                                     StatNameTagSpan prefix_tags, StatName prefix, StatName name,
                                     Histogram::Unit unit) {
  SymbolTable& symbol_table = scope.symbolTable();
  if (prefix_tags.empty()) {
    prefix = base_prefix;
  } else {
    ASSERT(!prefix.empty(),
           "When tags are supplied, the caller must supply the tagged prefix with the "
           "tag values interleaved.");
  }

  SymbolTable::StoragePtr full_name = symbol_table.join({prefix, name});
  SymbolTable::StoragePtr full_name_base = symbol_table.join({base_prefix, name});
  return scope.histogramFromTaggedName(StatName(full_name_base.get()), prefix_tags,
                                       StatName(full_name.get()), unit);
}

TextReadout& textReadoutFromTaggedPrefix(Scope& scope, StatName base_prefix,
                                         StatNameTagSpan prefix_tags, StatName prefix,
                                         StatName name) {
  SymbolTable& symbol_table = scope.symbolTable();
  if (prefix_tags.empty()) {
    prefix = base_prefix;
  } else {
    ASSERT(!prefix.empty(),
           "When tags are supplied, the caller must supply the tagged prefix with the "
           "tag values interleaved.");
  }

  SymbolTable::StoragePtr full_name = symbol_table.join({prefix, name});
  SymbolTable::StoragePtr full_name_base = symbol_table.join({base_prefix, name});
  return scope.textReadoutFromTaggedName(StatName(full_name_base.get()), prefix_tags,
                                         StatName(full_name.get()));
}

} // namespace Utility
} // namespace Stats
} // namespace Envoy
