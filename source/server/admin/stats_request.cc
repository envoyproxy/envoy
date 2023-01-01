#include "source/server/admin/stats_request.h"

namespace Envoy {
namespace Server {

StatsRequest::StatsRequest(Stats::Store& stats, const StatsParams& params,
                           UrlHandlerFn url_handler_fn)
    : StatsRequestBase(stats, params, url_handler_fn) {

  phases_ = {Phase{PhaseName::TextReadouts, "Text Readouts"},
             Phase{PhaseName::CountersAndGauges, "Counters and Gauges"},
             Phase{PhaseName::Histograms, "Histograms"}};

  switch (params_.type_) {
  case StatsType::TextReadouts:
  case StatsType::All:
    phase_ = 0;
    break;
  case StatsType::Counters:
  case StatsType::Gauges:
    phase_ = 1;
    break;
  case StatsType::Histograms:
    phase_ = 2;
    break;
  }
}

template <class StatType> Stats::IterateFn<StatType> StatsRequest::checkStat() {
  return [this](const Stats::RefcountPtr<StatType>& stat) -> bool {
    // check if used
    if (params_.used_only_ && !stat->used()) {
      return true;
    }

    // Capture the name if we did not early-exit due to used_only -- we'll use
    // the name for both filtering and for capturing the stat in the map.
    // stat->name() takes a symbol table lock and builds a string, so we only
    // want to call it once.
    //
    std::string name = stat->name();

    // check if filtered
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
}

Stats::IterateFn<Stats::TextReadout> StatsRequest::checkStatForTextReadout() {
  return checkStat<Stats::TextReadout>();
}

Stats::IterateFn<Stats::Gauge> StatsRequest::checkStatForGauge() {
  return checkStat<Stats::Gauge>();
}

Stats::IterateFn<Stats::Counter> StatsRequest::checkStatForCounter() {
  return checkStat<Stats::Counter>();
}

Stats::IterateFn<Stats::Histogram> StatsRequest::checkStatForHistogram() {
  return checkStat<Stats::Histogram>();
}

template <class SharedStatType>
void StatsRequest::renderStat(const std::string& name, Buffer::Instance& response,
                              const StatOrScopes& variant) {
  auto stat = absl::get<SharedStatType>(variant);
  render_->generate(response, name, stat->value());
  phase_stat_count_++;
}

void StatsRequest::processTextReadout(const std::string& name, Buffer::Instance& response,
                                      const StatOrScopes& variant) {
  renderStat<Stats::TextReadoutSharedPtr>(name, response, variant);
}

void StatsRequest::processCounter(const std::string& name, Buffer::Instance& response,
                                  const StatOrScopes& variant) {
  renderStat<Stats::CounterSharedPtr>(name, response, variant);
}

void StatsRequest::processGauge(const std::string& name, Buffer::Instance& response,
                                const StatOrScopes& variant) {
  renderStat<Stats::GaugeSharedPtr>(name, response, variant);
}

void StatsRequest::processHistogram(const std::string& name, Buffer::Instance& response,
                                    const StatOrScopes& variant) {
  auto histogram = absl::get<Stats::HistogramSharedPtr>(variant);
  auto parent_histogram = dynamic_cast<Stats::ParentHistogram*>(histogram.get());
  if (parent_histogram != nullptr) {
    render_->generate(response, name, *parent_histogram);
    ++phase_stat_count_;
  }
}

} // namespace Server
} // namespace Envoy
