#include "source/common/stats/isolated_store_impl.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "source/common/common/utility.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/stats/utility.h"

namespace Envoy {
namespace Stats {

IsolatedStoreImpl::IsolatedStoreImpl() : IsolatedStoreImpl(std::make_unique<SymbolTableImpl>()) {}

IsolatedStoreImpl::IsolatedStoreImpl(std::unique_ptr<SymbolTable>&& symbol_table)
    : IsolatedStoreImpl(*symbol_table) {
  symbol_table_storage_ = std::move(symbol_table);
}

static StatNameTagSpan tagSpanFromOpt(std::optional<StatNameTagSpan> tags) {
  return tags ? tags.value() : StatNameTagSpan{};
}

IsolatedStoreImpl::IsolatedStoreImpl(SymbolTable& symbol_table)
    : alloc_(symbol_table),
      counters_([this](const TagUtility::TagStatNameJoiner& joiner) -> CounterSharedPtr {
        return alloc_.makeCounter(joiner.nameWithTags(), joiner.tagExtractedName(),
                                  tagSpanFromOpt(joiner.effectiveTags()));
      }),
      gauges_([this](const TagUtility::TagStatNameJoiner& joiner,
                     Gauge::ImportMode import_mode) -> GaugeSharedPtr {
        return alloc_.makeGauge(joiner.nameWithTags(), joiner.tagExtractedName(),
                                tagSpanFromOpt(joiner.effectiveTags()), import_mode);
      }),
      histograms_([this](const TagUtility::TagStatNameJoiner& joiner,
                         Histogram::Unit unit) -> HistogramSharedPtr {
        return {new HistogramImpl(joiner.nameWithTags(), unit, *this, joiner.tagExtractedName(),
                                  tagSpanFromOpt(joiner.effectiveTags()))};
      }),
      text_readouts_([this](const TagUtility::TagStatNameJoiner& joiner,
                            TextReadout::Type) -> TextReadoutSharedPtr {
        return alloc_.makeTextReadout(joiner.nameWithTags(), joiner.tagExtractedName(),
                                      tagSpanFromOpt(joiner.effectiveTags()));
      }),
      null_counter_(symbol_table), null_gauge_(symbol_table), null_histogram_(symbol_table),
      null_text_readout_(symbol_table) {}

ScopeSharedPtr IsolatedStoreImpl::rootScope() {
  if (lazy_default_scope_ == nullptr) {
    StatNameManagedStorage name_storage("", symbolTable());
    lazy_default_scope_ = makeScope(name_storage.statName());
  }
  return lazy_default_scope_;
}

ConstScopeSharedPtr IsolatedStoreImpl::constRootScope() const {
  return const_cast<IsolatedStoreImpl*>(this)->rootScope();
}

IsolatedStoreImpl::~IsolatedStoreImpl() = default;

ScopeSharedPtr IsolatedScopeImpl::createScopeWithTaggedName(
    absl::string_view base_name, TagStringViewSpan name_tags, absl::string_view tagged_name,
    bool evictable, const ScopeStatsLimitSettings& limits, StatsMatcherSharedPtr matcher) {
  // Intern the string-based tag-extracted name, name_tags and (optional) tagged name into a
  // temporary pool, then delegate to the StatName-based variant.
  StatNamePool tag_pool(symbolTable());
  StatName stat_name = tag_pool.add(Utility::sanitizeStatsName(base_name));
  StatName stat_tagged_name;
  if (!name_tags.empty()) {
    // The tagged name is only meaningful when there are tags to interleave; otherwise it is
    // ignored and the TagStatNameJoiner will use the tag-extracted name as the flat tagged name.
    stat_tagged_name =
        tagged_name.empty() ? StatName() : tag_pool.add(Utility::sanitizeStatsName(tagged_name));
  }

  StatNameTagVec stat_name_tags;
  stat_name_tags.reserve(name_tags.size());
  for (const auto& [tag, value] : name_tags) {
    stat_name_tags.emplace_back(tag_pool.add(tag), tag_pool.add(value));
  }
  return scopeFromTaggedName(stat_name, stat_name_tags, stat_tagged_name, evictable, limits,
                             std::move(matcher));
}

ScopeSharedPtr IsolatedScopeImpl::scopeFromTaggedName(StatName base_name, StatNameTagSpan name_tags,
                                                      StatName tagged_name, bool,
                                                      const ScopeStatsLimitSettings&,
                                                      StatsMatcherSharedPtr matcher) {
  // Combine this scope's tag-extracted/tagged prefix with the new scope element to derive the
  // child's flat prefix.
  const TagUtility::TagStatNameJoiner joiner(prefix_.statName(), {}, prefix_.statName(), base_name,
                                             name_tags, tagged_name, symbolTable());

  // Use explicit matcher if provided; otherwise inherit scope_matcher_.
  StatsMatcherSharedPtr child_matcher = matcher ? std::move(matcher) : scope_matcher_;

  // The isolated store will not sink stats to external systems and the structured tags make no
  // sense to be kept in the scope.
  // No matter whether the caller provides tags or not, the isolated scope only keeps the final
  // joined tagged name but drops the tags self. This ensure the created stats also have the
  // correct full tagged name, but the tags won't be propagated to the child scopes or stats.
  ScopeSharedPtr scope = store_.makeScope(joiner.nameWithTags(), std::move(child_matcher));
  addScopeToStore(scope);
  return scope;
}

ScopeSharedPtr IsolatedStoreImpl::makeScope(StatName name, StatsMatcherSharedPtr matcher) {
  return std::make_shared<IsolatedScopeImpl>(name, *this, std::move(matcher));
}

} // namespace Stats
} // namespace Envoy
