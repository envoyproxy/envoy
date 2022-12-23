#pragma once

#include <vector>

#include "envoy/server/admin.h"

#include "source/server/admin/stats_params.h"
#include "source/server/admin/stats_render.h"
#include "source/server/admin/utils.h"
#include "source/server/admin/base_stats_request.h"

#include "absl/container/btree_map.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Server {

class PrometheusStatsRequest
    : public StatsRequestBase<
          std::vector<Stats::TextReadoutSharedPtr>, std::vector<Stats::CounterSharedPtr>,
          std::vector<Stats::GaugeSharedPtr>, std::vector<Stats::HistogramSharedPtr>> {

public:

  PrometheusStatsRequest(Stats::Store& stats, const StatsParams& params,
                         Stats::CustomStatNamespaces& custom_namespaces,
                         UrlHandlerFn url_handler_fn = nullptr);

  template <class StatType> void populateStatsFromScopes(const ScopeVec& scope_vec);

  template <class SharedStatType>
  void renderStat(const std::string& name, Buffer::Instance& response, const StatOrScopes& variant);

  void handleTextReadout(Buffer::Instance& response, const StatOrScopes& variant) override;

  void handleGauge(Buffer::Instance& response, const StatOrScopes& variant) override;

  void handleCounter(Buffer::Instance& response, const StatOrScopes& variant) override;

  void handleHistogram(Buffer::Instance& response, const StatOrScopes& variant) override;

  // PrometheusStatsRequest
  template <class SharedStatType>
  absl::optional<std::string> prefixedTagExtractedName(const StatOrScopes& variant);

private:
  Stats::CustomStatNamespaces& custom_namespaces_;
};

} // namespace Server
} // namespace Envoy
