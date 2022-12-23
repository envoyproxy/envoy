#include "source/server/admin/prometheus_stats_request.h"

#include <string>
#include <vector>
#include <iostream>


#include "source/server/admin/prometheus_stats.h"

#include "stats_params.h"

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

  phases_ = {Phase{PhaseName::Counters, "Counters"}, Phase{PhaseName::Gauges, "Gauges"},
             Phase{PhaseName::TextReadouts, "Text Readouts"},
             Phase{PhaseName::Histograms, "Histograms"}};

  switch (params_.type_) {
  case StatsType::All:
  case StatsType::Counters:
    phase_ = 0;
    break;
  case StatsType::Gauges:
    phase_ = 1;
    break;
  case StatsType::TextReadouts:
    phase_ = 2;
    break;
  case StatsType::Histograms:
    phase_ = 3;
    break;
  }
}

void PrometheusStatsRequest::handleTextReadout(Buffer::Instance& response,
                                               const StatOrScopes& variant) {
  if (params_.prometheus_text_readouts_) {
    auto prefixed_tag_extracted_name =
        prefixedTagExtractedName<Stats::TextReadoutSharedPtr>(variant);
    if (prefixed_tag_extracted_name.has_value()) {
      ++phase_stat_count_;
      renderStat<Stats::TextReadoutSharedPtr>(prefixed_tag_extracted_name.value(), response,
                                              variant);
    }
  }
}

void PrometheusStatsRequest::handleCounter(Buffer::Instance& response,
                                           const StatOrScopes& variant) {
  auto prefixed_tag_extracted_name = prefixedTagExtractedName<Stats::CounterSharedPtr>(variant);
  if (prefixed_tag_extracted_name.has_value()) {
    ++phase_stat_count_;
    renderStat<Stats::CounterSharedPtr>(prefixed_tag_extracted_name.value(), response, variant);
  }
}

void PrometheusStatsRequest::handleGauge(Buffer::Instance& response, const StatOrScopes& variant) {
  auto prefixed_tag_extracted_name = prefixedTagExtractedName<Stats::GaugeSharedPtr>(variant);
  if (prefixed_tag_extracted_name.has_value()) {
    ++phase_stat_count_;
    renderStat<Stats::GaugeSharedPtr>(prefixed_tag_extracted_name.value(), response, variant);
  }
}

void PrometheusStatsRequest::handleHistogram(Buffer::Instance& response,
                                             const StatOrScopes& variant) {
  auto histogram = absl::get<std::vector<Stats::HistogramSharedPtr>>(variant);
  auto prefixed_tag_extracted_name = prefixedTagExtractedName<Stats::HistogramSharedPtr>(variant);

  if (prefixed_tag_extracted_name.has_value()) {
    response.add(fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name.value(), "histogram"));
    phase_stat_count_++;
    std::sort(histogram.begin(), histogram.end(), metricLessThan);
    for (const auto& metric : histogram) {
      auto parent_histogram = dynamic_cast<Stats::ParentHistogram*>(metric.get());
      if (parent_histogram != nullptr) {
        render_->generate(response, prefixed_tag_extracted_name.value(), *parent_histogram);
      }
    }
  }
}

template <typename StatType>
void PrometheusStatsRequest::populateStatsFromScopes(const ScopeVec& scope_vec) {
  Stats::IterateFn<StatType> check_stat = [this](const Stats::RefcountPtr<StatType>& stat) -> bool {
    if (params_.used_only_ && !stat->used()) {
      return true;
    }

    if (params_.filter_ != nullptr) {
      if (!std::regex_search(stat->name(), *params_.filter_)) {
        return true;
      }
    } else if (params_.re2_filter_ != nullptr &&
               !re2::RE2::PartialMatch(stat->name(), *params_.re2_filter_)) {
      return true;
    }

    // at this point we know the stat should be "captured"
    const Stats::SymbolTable& global_symbol_table = stat->constSymbolTable();
    std::string tag_extracted_name = global_symbol_table.toString(stat->tagExtractedStatName());

    StatOrScopes variant = stat_map_.contains(tag_extracted_name)
                               ? stat_map_[tag_extracted_name]
                               : std::vector<Stats::RefcountPtr<StatType>>();
    absl::get<std::vector<Stats::RefcountPtr<StatType>>>(variant).emplace_back(stat);
    stat_map_[tag_extracted_name] = variant;
    return true;
  };

  for (const Stats::ConstScopeSharedPtr& scope : scope_vec) {
    scope->iterate(check_stat);
  }
}

template <class SharedStatType>
absl::optional<std::string>
PrometheusStatsRequest::prefixedTagExtractedName(const StatOrScopes& variant) {
  std::vector<SharedStatType> stat = absl::get<std::vector<SharedStatType>>(variant);
  const Stats::SymbolTable& global_symbol_table = stat.front()->constSymbolTable();
  std::string tag_extracted_name =
      global_symbol_table.toString(stat.front()->tagExtractedStatName());
  const absl::optional<std::string> prefixed_tag_extracted_name =
      Envoy::Server::PrometheusStatsFormatter::metricName(tag_extracted_name, custom_namespaces_);
  return prefixed_tag_extracted_name;
}

template <class SharedStatType>
void PrometheusStatsRequest::renderStat(const std::string& prefixed_tag_extracted_name,
                                        Buffer::Instance& response, const StatOrScopes& variant) {
  StatOrScopesIndex index = static_cast<StatOrScopesIndex>(variant.index());
  if (index == StatOrScopesIndex::Scopes || index == StatOrScopesIndex::Histogram) {
    IS_ENVOY_BUG("index of variant is Scopes or Histogram unexpectedly");
    return;
  }

  std::vector<SharedStatType> group = absl::get<std::vector<SharedStatType>>(variant);
  // sort to preserve preferred exposition format
  std::sort(group.begin(), group.end(), metricLessThan);

  // here we come to the actual rendering
  std::string type = index == StatOrScopesIndex::Counter ? "counter" : "gauge";
  response.add(fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name, type));

  PrometheusStatsRender* const prometheus_render =
      dynamic_cast<PrometheusStatsRender*>(render_.get());
  for (SharedStatType metric : group) {
    prometheus_render->generate(response, prefixed_tag_extracted_name, *metric.get());
  }
}

} // namespace Server
} // namespace Envoy
