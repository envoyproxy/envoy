#include "source/server/admin/prometheus_stats_request.h"

#include <string>
#include <vector>

#include "source/server/admin/prometheus_stats.h"

#include "stats_params.h"
#include "stats_render.h"

namespace Envoy {
namespace Server {

namespace {
bool metricLessThan(const Stats::RefcountPtr<Stats::Metric>& stat1,
                    const Stats::RefcountPtr<Stats::Metric>& stat2) {
  ASSERT(&stat1->constSymbolTable() == &stat2->constSymbolTable());
  return stat1->constSymbolTable().lessThan(stat1->statName(), stat2->statName());
}
} // namespace

PrometheusStatsRequest::PrometheusStatsRequest(Stats::Store& stats, const StatsParams& params,
                                               Stats::CustomStatNamespaces& custom_namespaces,
                                               UrlHandlerFn url_handler_fn)
    : StatsRequestBase(stats, params, url_handler_fn), custom_namespaces_(custom_namespaces) {

  // the "type" query param is ignored for prometheus stats, so always start from
  // counters; also, skip the TextReadouts phase unless that stat type is explicitly
  // requested via query param
  if (params_.prometheus_text_readouts_) {
    phases_ = {Phase{PhaseName::Counters, "Counters"}, Phase{PhaseName::Gauges, "Gauges"},
               Phase{PhaseName::TextReadouts, "Text Readouts"},
               Phase{PhaseName::Histograms, "Histograms"}};
  } else {
    phases_ = {Phase{PhaseName::Counters, "Counters"}, Phase{PhaseName::Gauges, "Gauges"},
               Phase{PhaseName::Histograms, "Histograms"}};
  }
  phase_ = 0;
}

template <class StatType> Stats::IterateFn<StatType> PrometheusStatsRequest::checkStat() {
  return [this](const Stats::RefcountPtr<StatType>& stat) -> bool {
    // check if unused
    if (params_.used_only_ && !stat->used()) {
      return true;
    }

    // check if filtered
    if (params_.filter_ != nullptr) {
      if (!std::regex_search(stat->name(), *params_.filter_)) {
        return true;
      }
    } else if (params_.re2_filter_ != nullptr &&
               !re2::RE2::PartialMatch(stat->name(), *params_.re2_filter_)) {
      return true;
    }

    // capture stat by either adding to a pre-existing variant or by creating a new variant and
    // storing it into the map
    const Stats::SymbolTable& global_symbol_table = stat->constSymbolTable();
    std::string tag_extracted_name = global_symbol_table.toString(stat->tagExtractedStatName());

    StatOrScopes variant = stat_map_.contains(tag_extracted_name)
                               ? stat_map_[tag_extracted_name]
                               : std::vector<Stats::RefcountPtr<StatType>>();
    absl::get<std::vector<Stats::RefcountPtr<StatType>>>(variant).emplace_back(stat);

    stat_map_[tag_extracted_name] = variant;
    return true;
  };
}

Stats::IterateFn<Stats::TextReadout> PrometheusStatsRequest::checkStatForTextReadout() {
  return checkStat<Stats::TextReadout>();
}

Stats::IterateFn<Stats::Gauge> PrometheusStatsRequest::checkStatForGauge() {
  return checkStat<Stats::Gauge>();
}

Stats::IterateFn<Stats::Counter> PrometheusStatsRequest::checkStatForCounter() {
  return checkStat<Stats::Counter>();
}

Stats::IterateFn<Stats::Histogram> PrometheusStatsRequest::checkStatForHistogram() {
  return checkStat<Stats::Histogram>();
}

template <class SharedStatType>
void PrometheusStatsRequest::renderStat(Buffer::Instance& response, const StatOrScopes& variant) {
  auto prefixed_tag_extracted_name = prefixedTagExtractedName<SharedStatType>(variant);
  if (prefixed_tag_extracted_name.has_value()) {
    PrometheusStatsRender* const prometheus_render =
        dynamic_cast<PrometheusStatsRender*>(render_.get());
    // increment stats count
    ++phase_stat_count_;

    // sort group
    std::vector<SharedStatType> group = absl::get<std::vector<SharedStatType>>(variant);
    std::sort(group.begin(), group.end(), metricLessThan);

    // render group
    StatOrScopesIndex index = static_cast<StatOrScopesIndex>(variant.index());
    std::string type = (index == StatOrScopesIndex::Counter) ? "counter" : "gauge";

    response.add(fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name.value(), type));
    for (SharedStatType metric : group) {
      prometheus_render->generate(response, prefixed_tag_extracted_name.value(), *metric.get());
    }
  }
}

void PrometheusStatsRequest::processTextReadout(Buffer::Instance& response,
                                                const StatOrScopes& variant) {
  renderStat<Stats::TextReadoutSharedPtr>(response, variant);
}

void PrometheusStatsRequest::processCounter(Buffer::Instance& response,
                                            const StatOrScopes& variant) {
  renderStat<Stats::CounterSharedPtr>(response, variant);
}

void PrometheusStatsRequest::processGauge(Buffer::Instance& response, const StatOrScopes& variant) {
  renderStat<Stats::GaugeSharedPtr>(response, variant);
}

void PrometheusStatsRequest::processHistogram(Buffer::Instance& response,
                                              const StatOrScopes& variant) {
  auto histogram = absl::get<std::vector<Stats::HistogramSharedPtr>>(variant);
  auto prefixed_tag_extracted_name = prefixedTagExtractedName<Stats::HistogramSharedPtr>(variant);

  if (prefixed_tag_extracted_name.has_value()) {
    // increment stats count
    phase_stat_count_++;

    // sort group
    std::sort(histogram.begin(), histogram.end(), metricLessThan);

    // render group
    response.add(fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name.value(), "histogram"));
    for (const auto& metric : histogram) {
      auto parent_histogram = dynamic_cast<Stats::ParentHistogram*>(metric.get());
      if (parent_histogram != nullptr) {
        render_->generate(response, prefixed_tag_extracted_name.value(), *parent_histogram);
      }
    }
  }
}

template <class SharedStatType>
absl::optional<std::string>
PrometheusStatsRequest::prefixedTagExtractedName(const StatOrScopes& variant) {
  std::vector<SharedStatType> group = absl::get<std::vector<SharedStatType>>(variant);

  if (!group.empty()) {
    const Stats::SymbolTable& global_symbol_table = group.front()->constSymbolTable();
    std::string tag_extracted_name =
        global_symbol_table.toString(group.front()->tagExtractedStatName());
    return Envoy::Server::PrometheusStatsFormatter::metricName(tag_extracted_name,
                                                               custom_namespaces_);
  }
  return {};
}

} // namespace Server
} // namespace Envoy
