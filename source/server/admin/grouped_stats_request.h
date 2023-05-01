#pragma once

#include "source/server/admin/stats_request.h"

namespace Envoy {
namespace Server {

// In the context of this class, a stat is represented by a group of
// gauges, counters etc. with the same tag-extracted name. This class
// is currently used for Prometheus stats only (and has some Prometheus-specific
// logic in it), but if needed could be generalized for other
// formats.
// TODO(rulex123): cleanup any Prometheus-specific logic if we decide to have a grouped view
// for HTML or JSON stats.
class GroupedStatsRequest
    : public StatsRequest<std::vector<Stats::TextReadoutSharedPtr>,
                          std::vector<Stats::CounterSharedPtr>, std::vector<Stats::GaugeSharedPtr>,
                          std::vector<Stats::HistogramSharedPtr>> {

public:
  GroupedStatsRequest(Stats::Store& stats, const StatsParams& params,
                      Stats::CustomStatNamespaces& custom_namespaces,
                      UrlHandlerFn url_handler_fn = nullptr);

protected:
  Stats::IterateFn<Stats::TextReadout> saveMatchingStatForTextReadout() override;
  Stats::IterateFn<Stats::Gauge> saveMatchingStatForGauge() override;
  Stats::IterateFn<Stats::Counter> saveMatchingStatForCounter() override;
  Stats::IterateFn<Stats::Histogram> saveMatchingStatForHistogram() override;
  template <class StatType> Stats::IterateFn<StatType> saveMatchingStat();

  void processTextReadout(const std::string& name, Buffer::Instance& response,
                          const StatOrScopes& variant) override;
  void processGauge(const std::string& name, Buffer::Instance& response,
                    const StatOrScopes& variant) override;
  void processCounter(const std::string& name, Buffer::Instance& response,
                      const StatOrScopes& variant) override;
  void processHistogram(const std::string& name, Buffer::Instance& response,
                        const StatOrScopes& variant) override;

  // GroupedStatsRequest
  template <class SharedStatType>
  absl::optional<std::string> prefixedTagExtractedName(const std::string& tag_extracted_name);

  template <class SharedStatType>
  void renderStat(const std::string& name, Buffer::Instance& response, const StatOrScopes& variant);

  void setRenderPtr(Http::ResponseHeaderMap& response_headers) override;
  StatsRenderBase& render() override {
    ASSERT(render_ != nullptr);
    return *render_;
  }

private:
  Stats::CustomStatNamespaces& custom_namespaces_;
  const Stats::SymbolTable& global_symbol_table_;
  std::unique_ptr<PrometheusStatsRender> render_;
};

} // namespace Server
} // namespace Envoy
