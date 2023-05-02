#include "source/server/admin/grouped_stats_request.h"

#include <string>
#include <vector>

#include "source/server/admin/stats_render.h"

namespace Envoy {
namespace Server {

GroupedStatsRequest::GroupedStatsRequest(Stats::Store& stats, const StatsParams& params,
                                         Stats::CustomStatNamespaces& custom_namespaces,
                                         UrlHandlerFn url_handler_fn)
    : StatsRequest(stats, params, url_handler_fn), custom_namespaces_(custom_namespaces),
      global_symbol_table_(stats.constSymbolTable()) {

  // The "type" query param is ignored for prometheus stats, so always start from
  // counters; also, skip the TextReadouts phase unless that stat type is explicitly
  // requested via query param.
  if (params_.prometheus_text_readouts_) {
    phases_ = {Phase::Counters, Phase::Gauges, Phase::TextReadouts, Phase::Histograms};
  } else {
    phases_ = {Phase::Counters, Phase::Gauges, Phase::Histograms};
  }
  phase_index_ = 0;
}

template <class StatType> Stats::IterateFn<StatType> GroupedStatsRequest::saveMatchingStat() {
  return [this](const Stats::RefcountPtr<StatType>& stat) -> bool {
    // Check if unused.
    if (params_.used_only_ && !stat->used()) {
      return true;
    }

    // Check if filtered.
    if (params_.re2_filter_ != nullptr &&
        !re2::RE2::PartialMatch(stat->name(), *params_.re2_filter_)) {
      return true;
    }

    // Capture stat.
    std::string tag_extracted_name = global_symbol_table_.toString(stat->tagExtractedStatName());
    stat_map_.insert({tag_extracted_name, std::vector<Stats::RefcountPtr<StatType>>({})});
    absl::get<std::vector<Stats::RefcountPtr<StatType>>>(stat_map_[tag_extracted_name])
        .emplace_back(stat);
    return true;
  };
}

Stats::IterateFn<Stats::TextReadout> GroupedStatsRequest::saveMatchingStatForTextReadout() {
  return saveMatchingStat<Stats::TextReadout>();
}

Stats::IterateFn<Stats::Gauge> GroupedStatsRequest::saveMatchingStatForGauge() {
  return saveMatchingStat<Stats::Gauge>();
}

Stats::IterateFn<Stats::Counter> GroupedStatsRequest::saveMatchingStatForCounter() {
  return saveMatchingStat<Stats::Counter>();
}

Stats::IterateFn<Stats::Histogram> GroupedStatsRequest::saveMatchingStatForHistogram() {
  return saveMatchingStat<Stats::Histogram>();
}

template <class SharedStatType>
void GroupedStatsRequest::renderStat(const std::string& name, Buffer::Instance& response,
                                     const StatOrScopes& variant) {
  auto prefixed_tag_extracted_name = prefixedTagExtractedName<SharedStatType>(name);
  if (prefixed_tag_extracted_name.has_value()) {
    ++phase_stat_count_;

    // Sort group.
    std::vector<SharedStatType> group = absl::get<std::vector<SharedStatType>>(variant);
    global_symbol_table_.sortByStatNames<SharedStatType>(
        group.begin(), group.end(),
        [](const SharedStatType& stat_ptr) -> Stats::StatName { return stat_ptr->statName(); });

    // Render group.
    render_->generate(response, prefixed_tag_extracted_name.value(), group);
  }
}

void GroupedStatsRequest::processTextReadout(const std::string& name, Buffer::Instance& response,
                                             const StatOrScopes& variant) {
  renderStat<Stats::TextReadoutSharedPtr>(name, response, variant);
}

void GroupedStatsRequest::processCounter(const std::string& name, Buffer::Instance& response,
                                         const StatOrScopes& variant) {

  renderStat<Stats::CounterSharedPtr>(name, response, variant);
}

void GroupedStatsRequest::processGauge(const std::string& name, Buffer::Instance& response,
                                       const StatOrScopes& variant) {
  renderStat<Stats::GaugeSharedPtr>(name, response, variant);
}

void GroupedStatsRequest::processHistogram(const std::string& name, Buffer::Instance& response,
                                           const StatOrScopes& variant) {
  renderStat<Stats::HistogramSharedPtr>(name, response, variant);
}

template <class SharedStatType>
absl::optional<std::string>
GroupedStatsRequest::prefixedTagExtractedName(const std::string& tag_extracted_name) {
  return Envoy::Server::PrometheusStatsRender::metricName(tag_extracted_name, custom_namespaces_);
}

void GroupedStatsRequest::setRenderPtr(Http::ResponseHeaderMap&) {
  ASSERT(params_.format_ == StatsFormat::Prometheus);
  render_ = std::make_unique<PrometheusStatsRender>();
}

} // namespace Server
} // namespace Envoy
