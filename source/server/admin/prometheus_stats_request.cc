#include "source/server/admin/prometheus_stats_request.h"

#include <iostream>
#include <string>
#include <vector>

#include "source/server/admin/prometheus_stats.h"

#include "prometheus_stats_render.h"
#include "stats_params.h"

#ifdef ENVOY_ADMIN_HTML
#include "source/server/admin/stats_html_render.h"
#endif

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
    : params_(params), stats_(stats), url_handler_fn_(url_handler_fn), custom_namespaces_(custom_namespaces){
  switch (params_.type_) {
  case StatsType::TextReadouts:
    phase_ = Phase::TextReadouts;
    break;
  case StatsType::All:
  case StatsType::Counters:
  case StatsType::Gauges:
    phase_ = Phase::CountersAndGauges;
    break;
  case StatsType::Histograms:
    phase_ = Phase::Histograms;
    break;
  }
}

Http::Code PrometheusStatsRequest::start(Http::ResponseHeaderMap&) {
  if (params_.format_ != StatsFormat::Prometheus) {
    IS_ENVOY_BUG("only Prometheus case is supported");
    return Http::Code::BadRequest;
  }

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

bool PrometheusStatsRequest::nextChunk(Buffer::Instance& response) {
  if (response_.length() > 0) {
    ASSERT(response.length() == 0);
    response.move(response_);
    ASSERT(response_.length() == 0);
  }

  // nextChunk's contract is to add up to chunk_size_ additional bytes. The
  // caller is not required to drain the bytes after each call to nextChunk.
  const uint64_t starting_response_length = response.length();
  while (response.length() - starting_response_length < chunk_size_) {
    while (stat_map_.empty()) {
      if (phase_stat_count_ == 0) {
        render_->noStats(response, phase_string_);
      } else {
        phase_stat_count_ = 0;
      }
      if (params_.type_ != StatsType::All) {
        render_->finalize(response);
        return false;
      }
      switch (phase_) {
      case Phase::TextReadouts:
        phase_ = Phase::Histograms;
        phase_string_ = "Histograms";
        startPhase();
        break;
      case Phase::CountersAndGauges:
        phase_ = Phase::TextReadouts;
        phase_string_ = "Text Readouts";
        startPhase();
        break;
      case Phase::Histograms:
        render_->finalize(response);
        return false;
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
      stat_map_.erase(iter);
      populateStatsForCurrentPhase(absl::get<ScopeVec>(variant));
      break;
    case StatOrScopesIndex::TextReadout: {
      if (params_.prometheus_text_readouts_) {
        auto prefixed_tag_extracted_name =
            prefixedTagExtractedName<Stats::TextReadoutSharedPtr>(variant);
        if (prefixed_tag_extracted_name.has_value()) {
          ++phase_stat_count_;
          renderStat<Stats::TextReadoutSharedPtr>(prefixed_tag_extracted_name.value(), response,
                                                  variant);
        }
      }
      stat_map_.erase(iter);
      break;
    }
    case StatOrScopesIndex::Counter: {
      auto prefixed_tag_extracted_name = prefixedTagExtractedName<Stats::CounterSharedPtr>(variant);
      if (prefixed_tag_extracted_name.has_value()) {
        ++phase_stat_count_;
        renderStat<Stats::CounterSharedPtr>(prefixed_tag_extracted_name.value(), response, variant);
      }
      stat_map_.erase(iter);
      break;
    }
    case StatOrScopesIndex::Gauge: {
      auto prefixed_tag_extracted_name = prefixedTagExtractedName<Stats::GaugeSharedPtr>(variant);
      if (prefixed_tag_extracted_name.has_value()) {
        ++phase_stat_count_;
        renderStat<Stats::GaugeSharedPtr>(prefixed_tag_extracted_name.value(), response, variant);
      }
      stat_map_.erase(iter);
      break;
    }
    case StatOrScopesIndex::Histogram: {
      auto histogram = absl::get<std::vector<Stats::HistogramSharedPtr>>(variant);
      auto prefixed_tag_extracted_name =
          prefixedTagExtractedName<Stats::HistogramSharedPtr>(variant);

      if (prefixed_tag_extracted_name.has_value()) {
        response.add(
            fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name.value(), "histogram"));
        phase_stat_count_++;
        std::sort(histogram.begin(), histogram.end(), metricLessThan);
        for (const auto& metric : histogram) {
          auto parent_histogram = dynamic_cast<Stats::ParentHistogram*>(metric.get());
          if (parent_histogram != nullptr) {
            render_->generate(response, prefixed_tag_extracted_name.value(), *parent_histogram);
          }
        }
      }
      stat_map_.erase(iter);
    }
    }
  }
  return true;
}

void PrometheusStatsRequest::startPhase() {
  ASSERT(stat_map_.empty());

  // Insert all the scopes in the alphabetically ordered map. As we iterate
  // through the map we'll erase the scopes and replace them with the stats held
  // in the scopes.
  for (const Stats::ConstScopeSharedPtr& scope : scopes_) {
    StatOrScopes& variant = stat_map_[stats_.symbolTable().toString(scope->prefix())];
    if (variant.index() == absl::variant_npos) {
      variant = ScopeVec();
    }
    absl::get<ScopeVec>(variant).emplace_back(scope);
  }
}

void PrometheusStatsRequest::populateStatsForCurrentPhase(const ScopeVec& scope_vec) {
  std::cout << "in populateStatsForCurrentPhase - " << phase_string_ << std::endl;

  switch (phase_) {
  case Phase::TextReadouts:
    populateStatsFromScopes<Stats::TextReadout>(scope_vec);
    break;
  case Phase::CountersAndGauges:
    if (params_.type_ != StatsType::Gauges) {
      populateStatsFromScopes<Stats::Counter>(scope_vec);
    }
    if (params_.type_ != StatsType::Counters) {
      populateStatsFromScopes<Stats::Gauge>(scope_vec);
    }
    break;
  case Phase::Histograms:
    populateStatsFromScopes<Stats::Histogram>(scope_vec);
    break;
  }
}

template <class StatType>
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

    std::cout << "in check_stat function: tag extracted name is " << tag_extracted_name << std::endl;

    StatOrScopes variant = stat_map_.contains(tag_extracted_name)
                               ? stat_map_[tag_extracted_name]
                               : std::vector<Stats::RefcountPtr<StatType>>();
    absl::get<std::vector<Stats::RefcountPtr<StatType>>>(variant).emplace_back(stat);
    stat_map_[tag_extracted_name]=variant;
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
      PrometheusStatsFormatter::metricName(tag_extracted_name, custom_namespaces_);
  return prefixed_tag_extracted_name;
}

template <class SharedStatType>
void PrometheusStatsRequest::renderStat(const std::string& prefixed_tag_extracted_name,
                                        Buffer::Instance& response, StatOrScopes& variant) {
  std::cout << "in renderStat method - prefixed tag name is " << prefixed_tag_extracted_name << std::endl;

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
  for (SharedStatType metric : group) {
    render_->generate(response, prefixed_tag_extracted_name, *metric.get());
  }
}

} // namespace Server
} // namespace Envoy
