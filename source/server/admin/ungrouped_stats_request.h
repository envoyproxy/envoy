#pragma once

#include "source/server/admin/stats_request.h"

namespace Envoy {
namespace Server {

// In the context of this class, a stat is represented by an individual
// gauge, counter etc.; this is in contrast to the GroupedStatsRequest class,
// where a stat is a group of gauges, counters etc. with a common tag-extracted name.
// This class is currently used for all non-Prometheus formats (e.g. text, JSON).
class UngroupedStatsRequest
    : public StatsRequest<Stats::TextReadoutSharedPtr, Stats::CounterSharedPtr,
                          Stats::GaugeSharedPtr, Stats::HistogramSharedPtr> {

public:
  UngroupedStatsRequest(Stats::Store& stats, const StatsParams& params,
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

  template <class SharedStatType>
  void renderStat(const std::string& name, Buffer::Instance& response, const StatOrScopes& variant);

  void setRenderPtr(Http::ResponseHeaderMap& response_headers) override;
  StatsRenderBase& render() override {
    ASSERT(render_ != nullptr);
    return *render_;
  }

private:
  std::unique_ptr<StatsRender> render_;
};
} // namespace Server
} // namespace Envoy
