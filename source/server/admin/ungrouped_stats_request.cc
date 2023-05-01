#include "source/server/admin/ungrouped_stats_request.h"

#ifdef ENVOY_ADMIN_HTML
#include "source/server/admin/stats_html_render.h"
#endif

namespace Envoy {
namespace Server {

UngroupedStatsRequest::UngroupedStatsRequest(Stats::Store& stats, const StatsParams& params,
                                             UrlHandlerFn url_handler_fn)
    : StatsRequest(stats, params, url_handler_fn) {
  phases_ = {Phase::TextReadouts, Phase::CountersAndGauges, Phase::Histograms};
  switch (params_.type_) {
  case StatsType::TextReadouts:
  case StatsType::All:
    phase_index_ = 0;
    break;
  case StatsType::Counters:
  case StatsType::Gauges:
    phase_index_ = 1;
    break;
  case StatsType::Histograms:
    phase_index_ = 2;
    break;
  }
}

template <class StatType> Stats::IterateFn<StatType> UngroupedStatsRequest::saveMatchingStat() {
  return [this](const Stats::RefcountPtr<StatType>& stat) -> bool {
    // Check if used.
    if (params_.used_only_ && !stat->used()) {
      return true;
    }

    // Capture the name if we did not early-exit due to used_only -- we'll use
    // the name for both filtering and for capturing the stat in the map.
    // stat->name() takes a symbol table lock and builds a string, so we only
    // want to call it once.
    //
    // This duplicates logic in saveMatchingStat in grouped_stats_request.cc, but
    // differs in that Prometheus only uses stat->name() for filtering, not
    // rendering, so it only grabs the name if there's a filter.
    std::string name = stat->name();

    // Check if filtered.
    if (params_.re2_filter_ != nullptr && !re2::RE2::PartialMatch(name, *params_.re2_filter_)) {
      return true;
    }

    stat_map_[name] = stat;
    return true;
  };
}

Stats::IterateFn<Stats::TextReadout> UngroupedStatsRequest::saveMatchingStatForTextReadout() {
  return saveMatchingStat<Stats::TextReadout>();
}

Stats::IterateFn<Stats::Gauge> UngroupedStatsRequest::saveMatchingStatForGauge() {
  return saveMatchingStat<Stats::Gauge>();
}

Stats::IterateFn<Stats::Counter> UngroupedStatsRequest::saveMatchingStatForCounter() {
  return saveMatchingStat<Stats::Counter>();
}

Stats::IterateFn<Stats::Histogram> UngroupedStatsRequest::saveMatchingStatForHistogram() {
  return saveMatchingStat<Stats::Histogram>();
}

template <class SharedStatType>
void UngroupedStatsRequest::renderStat(const std::string& name, Buffer::Instance& response,
                                       const StatOrScopes& variant) {
  auto stat = absl::get<SharedStatType>(variant);
  render_.get()->generate(response, name, stat->value());
  phase_stat_count_++;
}

void UngroupedStatsRequest::processTextReadout(const std::string& name, Buffer::Instance& response,
                                               const StatOrScopes& variant) {
  renderStat<Stats::TextReadoutSharedPtr>(name, response, variant);
}

void UngroupedStatsRequest::processCounter(const std::string& name, Buffer::Instance& response,
                                           const StatOrScopes& variant) {
  renderStat<Stats::CounterSharedPtr>(name, response, variant);
}

void UngroupedStatsRequest::processGauge(const std::string& name, Buffer::Instance& response,
                                         const StatOrScopes& variant) {
  renderStat<Stats::GaugeSharedPtr>(name, response, variant);
}

void UngroupedStatsRequest::processHistogram(const std::string& name, Buffer::Instance& response,
                                             const StatOrScopes& variant) {
  auto histogram = absl::get<Stats::HistogramSharedPtr>(variant);
  auto parent_histogram = dynamic_cast<Stats::ParentHistogram*>(histogram.get());
  if (parent_histogram != nullptr) {
    dynamic_cast<StatsRender*>(render_.get())->generate(response, name, *parent_histogram);
    ++phase_stat_count_;
  }
}

void UngroupedStatsRequest::setRenderPtr(Http::ResponseHeaderMap& response_headers) {
  switch (params_.format_) {
  case StatsFormat::Json:
    render_ = std::make_unique<StatsJsonRender>(response_headers, response_, params_);
    break;
  case StatsFormat::Text:
    render_ = std::make_unique<StatsTextRender>(params_);
    break;
#ifdef ENVOY_ADMIN_HTML
  case StatsFormat::Html: {
    auto html_render = std::make_unique<StatsHtmlRender>(response_headers, response_, params_);
    html_render->setupStatsPage(url_handler_fn_(), params_, response_);
    render_ = std::move(html_render);
    break;
  }
#endif
  case StatsFormat::Prometheus:
    IS_ENVOY_BUG("reached Prometheus case in switch unexpectedly");
    break;
  }
}

} // namespace Server
} // namespace Envoy
