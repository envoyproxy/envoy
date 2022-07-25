#include "source/server/admin/stats_request.h"

#ifdef ENVOY_ADMIN_HTML
#include "source/server/admin/stats_html_render.h"
#endif

namespace Envoy {
namespace Server {

StatsRequest::StatsRequest(Stats::Store& stats, const StatsParams& params,
                           UrlHandlerFn url_handler_fn)
    : params_(params), stats_(stats), url_handler_fn_(url_handler_fn) {
  switch (params_.type_) {
  case StatsType::TextReadouts:
  case StatsType::All:
    phase_ = Phase::TextReadouts;
    break;
  case StatsType::Counters:
  case StatsType::Gauges:
    phase_ = Phase::CountersAndGauges;
    break;
  case StatsType::Histograms:
    phase_ = Phase::Histograms;
    break;
  }
}

Http::Code StatsRequest::start(Http::ResponseHeaderMap& response_headers) {
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
    html_render->setSubmitOnChange(true);
    html_render->tableBegin(response_);
    html_render->urlHandler(response_, url_handler_fn_(), params_.query_);
    html_render->tableEnd(response_);
    html_render->startPre(response_);
    render_.reset(html_render.release());
    break;
  }
#endif
  case StatsFormat::Prometheus:
    // TODO(#16139): once Prometheus shares this algorithm here, this becomes a legitimate choice.
    IS_ENVOY_BUG("reached Prometheus case in switch unexpectedly");
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

bool StatsRequest::nextChunk(Buffer::Instance& response) {
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
        phase_ = Phase::CountersAndGauges;
        phase_string_ = "Counters and Gauges";
        startPhase();
        break;
      case Phase::CountersAndGauges:
        phase_ = Phase::Histograms;
        phase_string_ = "Histograms";
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
    case StatOrScopesIndex::TextReadout:
      renderStat<Stats::TextReadoutSharedPtr>(iter->first, response, variant);
      stat_map_.erase(iter);
      ++phase_stat_count_;
      break;
    case StatOrScopesIndex::Counter:
      renderStat<Stats::CounterSharedPtr>(iter->first, response, variant);
      stat_map_.erase(iter);
      ++phase_stat_count_;
      break;
    case StatOrScopesIndex::Gauge:
      renderStat<Stats::GaugeSharedPtr>(iter->first, response, variant);
      stat_map_.erase(iter);
      ++phase_stat_count_;
      break;
    case StatOrScopesIndex::Histogram: {
      auto histogram = absl::get<Stats::HistogramSharedPtr>(variant);
      auto parent_histogram = dynamic_cast<Stats::ParentHistogram*>(histogram.get());
      if (parent_histogram != nullptr) {
        render_->generate(response, iter->first, *parent_histogram);
        ++phase_stat_count_;
      }
      stat_map_.erase(iter);
    }
    }
  }
  return true;
}

void StatsRequest::startPhase() {
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

void StatsRequest::populateStatsForCurrentPhase(const ScopeVec& scope_vec) {
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

template <class StatType> void StatsRequest::populateStatsFromScopes(const ScopeVec& scope_vec) {
  Stats::IterateFn<StatType> check_stat = [this](const Stats::RefcountPtr<StatType>& stat) -> bool {
    if (params_.used_only_ && !stat->used()) {
      return true;
    }

    // Capture the name if we did not early-exit due to used_only -- we'll use
    // the name for both filtering and for capturing the stat in the map.
    // stat->name() takes a symbol table lock and builds a string, so we only
    // want to call it once.
    //
    // This duplicates logic in shouldShowMetric in prometheus_stats.cc, but
    // differs in that Prometheus only uses stat->name() for filtering, not
    // rendering, so it only grab the name if there's a filter.
    std::string name = stat->name();
    if (params_.filter_ != nullptr) {
      if (!std::regex_search(name, *params_.filter_)) {
        return true;
      }
    } else if (params_.re2_filter_ != nullptr &&
               !re2::RE2::PartialMatch(name, *params_.re2_filter_)) {
      return true;
    }
    stat_map_[name] = stat;
    return true;
  };
  for (const Stats::ConstScopeSharedPtr& scope : scope_vec) {
    scope->iterate(check_stat);
  }
}

template <class SharedStatType>
void StatsRequest::renderStat(const std::string& name, Buffer::Instance& response,
                              StatOrScopes& variant) {
  auto stat = absl::get<SharedStatType>(variant);
  render_->generate(response, name, stat->value());
}

} // namespace Server
} // namespace Envoy
