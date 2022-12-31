#pragma once

#include "envoy/server/admin.h"

#include "source/server/admin/base_stats_request.h"
#include "source/server/admin/stats_params.h"
#include "source/server/admin/stats_render.h"
#include "source/server/admin/utils.h"

#include "absl/container/btree_map.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Server {

class StatsRequest : public StatsRequestBase<Stats::TextReadoutSharedPtr, Stats::CounterSharedPtr,
                                             Stats::GaugeSharedPtr, Stats::HistogramSharedPtr> {

public:
  StatsRequest(Stats::Store& stats, const StatsParams& params,
               UrlHandlerFn url_handler_fn = nullptr);

  Stats::IterateFn<Stats::TextReadout> checkStatForTextReadout() override;
  Stats::IterateFn<Stats::Gauge> checkStatForGauge() override;
  Stats::IterateFn<Stats::Counter> checkStatForCounter() override;
  Stats::IterateFn<Stats::Histogram> checkStatForHistogram() override;
  template <class StatType> Stats::IterateFn<StatType> checkStat();

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
};
} // namespace Server
} // namespace Envoy
