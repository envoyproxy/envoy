#include "source/server/admin/stats_request.h"

namespace Envoy {
namespace Server {

template <class TextReadoutType, class CounterType, class GaugeType, class HistogramType>
StatsRequest<TextReadoutType, CounterType, GaugeType, HistogramType>::StatsRequest(
    Stats::Store& stats, const StatsParams& params, UrlHandlerFn url_handler_fn)
    : params_(params), url_handler_fn_(url_handler_fn), stats_(stats) {}

template <class TextReadoutTyoe, class CounterType, class GaugeType, class HistogramType>
Http::Code StatsRequest<TextReadoutTyoe, CounterType, GaugeType, HistogramType>::start(
    Http::ResponseHeaderMap& response_headers) {
  setRenderPtr(response_headers);
#ifdef ENVOY_ADMIN_HTML
  if (params_.format_ == StatsFormat::ActiveHtml) {
    return Http::Code::OK;
  }
#endif

  // Populate the top-level scopes and the stats underneath any scopes with an empty name.
  // We will have to de-dup, but we can do that after sorting.
  //
  // First capture all the scopes and hold onto them with a SharedPtr so they
  // can't be deleted after the initial iteration.
  stats_.forEachScope(
      [this](size_t s) { scopes_.reserve(s); },
      [this](const Stats::Scope& scope) { scopes_.emplace_back(scope.getConstShared()); });

  startPhase();
  return Http::Code::OK;
}

template <class TextReadoutTyoe, class CounterType, class GaugeType, class HistogramType>
bool StatsRequest<TextReadoutTyoe, CounterType, GaugeType, HistogramType>::nextChunk(
    Buffer::Instance& response) {
  if (response_.length() > 0) {
    ASSERT(response.length() == 0);
    response.move(response_);
    ASSERT(response_.length() == 0);
  }

  // nextChunk's contract is to add up to chunk_size_ additional bytes. The
  // caller is not required to drain the bytes after each call to nextChunk.
  StatsRenderBase& stats_render = render();
  const uint64_t starting_response_length = response.length();
  while (response.length() - starting_response_length < chunk_size_) {
    while (stat_map_.empty()) {
      if (phase_stat_count_ == 0) {
        stats_render.noStats(response, phase_labels_[phases_.at(phase_index_)]);
      } else {
        phase_stat_count_ = 0;
      }
      if (params_.type_ != StatsType::All) {
        stats_render.finalize(response);
        return false;
      }

      // Check if we are at the last phase: in that case, we are done;
      // if not, increment phase index and start next phase.
      if (phase_index_ == phases_.size() - 1) {
        stats_render.finalize(response);
        return false;
      } else {
        phase_index_++;
        startPhase();
      }
    }

    auto iter = stat_map_.begin();
    StatOrScopes variant = std::move(iter->second);
    StatOrScopesIndex index = static_cast<StatOrScopesIndex>(variant.index());

    switch (index) {
    case StatOrScopesIndex::Scopes:
      // Erase the current element before adding new ones, as absl::btree_map
      // does not have stable iterators. When we hit leaf stats we will erase
      // second, so that we can use the name held as a map key, and don't need
      // to re-serialize the name from the symbol table.
      //ENVOY_LOG_MISC(error, "Processing scope '{}' phase {}", iter->first, phase_index_);
      stat_map_.erase(iter);
      populateStatsForCurrentPhase(absl::get<ScopeVec>(variant));
      break;
    case StatOrScopesIndex::TextReadout:
      processTextReadout(iter->first, response, variant);
      stat_map_.erase(iter);
      break;
    case StatOrScopesIndex::Counter:
      //ENVOY_LOG_MISC(error, "Processing counter {} phase {}", iter->first, phase_index_);
      processCounter(iter->first, response, variant);
      stat_map_.erase(iter);
      break;
    case StatOrScopesIndex::Gauge:
      //ENVOY_LOG_MISC(error, "Processing gauge {} phase {}", iter->first, phase_index_);
      processGauge(iter->first, response, variant);
      stat_map_.erase(iter);
      break;
    case StatOrScopesIndex::Histogram:
      processHistogram(iter->first, response, variant);
      stat_map_.erase(iter);
      break;
    }
  }
  return true;
}

template <class TextReadoutTyoe, class CounterType, class GaugeType, class HistogramType>
void StatsRequest<TextReadoutTyoe, CounterType, GaugeType, HistogramType>::startPhase() {
  ASSERT(stat_map_.empty());

  // Insert all the scopes in the alphabetically ordered map. As we iterate
  // through the map we'll erase the scopes and replace them with the stats held
  // in the scopes.
  for (const Stats::ConstScopeSharedPtr& scope : scopes_) {
    // The operator[] of btree_map runs a try_emplace behind the scenes,
    // inserting the variant into the map when the lookup key does not exist.
    StatOrScopes& variant = stat_map_[stats_.symbolTable().toString(scope->prefix())];
    ASSERT(static_cast<StatOrScopesIndex>(variant.index()) == StatOrScopesIndex::Scopes);
    absl::get<ScopeVec>(variant).emplace_back(scope);
  }
}

template <class TextReadoutTyoe, class CounterType, class GaugeType, class HistogramType>
void StatsRequest<TextReadoutTyoe, CounterType, GaugeType,
                  HistogramType>::populateStatsForCurrentPhase(const ScopeVec& scope_vec) {
  Phase current_phase = phases_.at(phase_index_);
  for (const Stats::ConstScopeSharedPtr& scope : scope_vec) {
    switch (current_phase) {
    case Phase::TextReadouts:
      scope->iterate(saveMatchingStatForTextReadout());
      break;
    case Phase::CountersAndGauges:
      if (params_.type_ != StatsType::Gauges) {
        scope->iterate(saveMatchingStatForCounter());
      }
      if (params_.type_ != StatsType::Counters) {
        scope->iterate(saveMatchingStatForGauge());
      }
      break;
    case Phase::Counters:
      scope->iterate(saveMatchingStatForCounter());
      break;
    case Phase::Gauges:
      scope->iterate(saveMatchingStatForGauge());
      break;
    case Phase::Histograms:
      scope->iterate(saveMatchingStatForHistogram());
      break;
    }
  }
}

template class StatsRequest<Stats::TextReadoutSharedPtr, Stats::CounterSharedPtr,
                            Stats::GaugeSharedPtr, Stats::HistogramSharedPtr>;

template class StatsRequest<
    std::vector<Stats::TextReadoutSharedPtr>, std::vector<Stats::CounterSharedPtr>,
    std::vector<Stats::GaugeSharedPtr>, std::vector<Stats::HistogramSharedPtr>>;

} // namespace Server
} // namespace Envoy
