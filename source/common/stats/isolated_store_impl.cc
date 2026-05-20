#include "source/common/stats/isolated_store_impl.h"

#include <algorithm>
#include <cstring>
#include <string>

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

static StatNameTagSpan tagSpanFromOpt(absl::optional<StatNameTagSpan> tags) {
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

ScopeSharedPtr IsolatedScopeImpl::createScope(const std::string& name, bool,
                                              const ScopeStatsLimitSettings& limits,
                                              StatsMatcherSharedPtr matcher) {
  StatNameManagedStorage stat_name_storage(Utility::sanitizeStatsName(name), symbolTable());
  return scopeFromStatName(stat_name_storage.statName(), false, limits, std::move(matcher));
}

ScopeSharedPtr IsolatedScopeImpl::scopeFromStatName(StatName name, bool,
                                                    const ScopeStatsLimitSettings&,
                                                    StatsMatcherSharedPtr matcher) {
  SymbolTable::StoragePtr prefix_name_storage = symbolTable().join({prefix(), name});
  // Use explicit matcher if provided; otherwise inherit scope_matcher_.
  StatsMatcherSharedPtr child_matcher = matcher ? std::move(matcher) : scope_matcher_;
  ScopeSharedPtr scope =
      store_.makeScope(StatName(prefix_name_storage.get()), std::move(child_matcher));
  addScopeToStore(scope);
  return scope;
}

ScopeSharedPtr IsolatedStoreImpl::makeScope(StatName name, StatsMatcherSharedPtr matcher) {
  return std::make_shared<IsolatedScopeImpl>(name, *this, std::move(matcher));
}

} // namespace Stats
} // namespace Envoy
