#include "source/server/admin/grouped_stats_request.h"

#include <string>
#include <vector>

#include "source/server/admin/prometheus_stats.h"

#include "stats_params.h"
#include "stats_render.h"

namespace Envoy {
namespace Server {

GroupedStatsRequest::GroupedStatsRequest(Stats::Store& stats, const StatsParams& params,
                                         Stats::CustomStatNamespaces& custom_namespaces,
                                         UrlHandlerFn url_handler_fn)
    : StatsRequest(stats, params, url_handler_fn), custom_namespaces_(custom_namespaces),
      global_symbol_table_(stats.constSymbolTable()) {

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

template <class StatType> Stats::IterateFn<StatType> GroupedStatsRequest::saveMatchingStat() {
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

    // capture stat by either adding to a pre-existing variant or by creating a new variant, and
    std::string tag_extracted_name = global_symbol_table_.toString(stat->tagExtractedStatName());

    StatOrScopes& variant = stat_map_[tag_extracted_name];
    if (variant.index() == absl::variant_npos) {
      variant = std::vector<Stats::RefcountPtr<StatType>>({stat});
    } else {
      absl::get<std::vector<Stats::RefcountPtr<StatType>>>(variant).emplace_back(stat);
    }
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
    PrometheusStatsRender* const prometheus_render =
        dynamic_cast<PrometheusStatsRender*>(render_.get());
    // increment stats count
    ++phase_stat_count_;

    // sort group
    std::vector<SharedStatType> group = absl::get<std::vector<SharedStatType>>(variant);
    std::sort(group.begin(), group.end(),
              [this](const Stats::RefcountPtr<Stats::Metric>& stat1,
                     const Stats::RefcountPtr<Stats::Metric>& stat2) -> bool {
                return global_symbol_table_.lessThan(stat1->statName(), stat2->statName());
              });

    // render group
    StatOrScopesIndex index = static_cast<StatOrScopesIndex>(variant.index());
    std::string type = (index == StatOrScopesIndex::Counter) ? "counter" : "gauge";

    response.add(fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name.value(), type));
    for (SharedStatType metric : group) {
      prometheus_render->generate(response, prefixed_tag_extracted_name.value(), *metric.get());
    }
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
  auto histogram = absl::get<std::vector<Stats::HistogramSharedPtr>>(variant);
  auto prefixed_tag_extracted_name = prefixedTagExtractedName<Stats::HistogramSharedPtr>(name);

  if (prefixed_tag_extracted_name.has_value()) {
    // increment stats count
    phase_stat_count_++;

    // sort group
    std::sort(histogram.begin(), histogram.end(),
              [this](const Stats::RefcountPtr<Stats::Metric>& stat1,
                     const Stats::RefcountPtr<Stats::Metric>& stat2) -> bool {
                return global_symbol_table_.lessThan(stat1->statName(), stat2->statName());
              });

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
GroupedStatsRequest::prefixedTagExtractedName(const std::string& tag_extracted_name) {
  return Envoy::Server::PrometheusStatsFormatter::metricName(tag_extracted_name,
                                                             custom_namespaces_);
}

} // namespace Server
} // namespace Envoy
