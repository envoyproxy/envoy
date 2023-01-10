#pragma once

#include <vector>

#include "envoy/server/admin.h"

#include "source/server/admin/stats_params.h"
#include "source/server/admin/stats_render.h"
#include "source/server/admin/stats_request.h"
#include "source/server/admin/utils.h"

#include "absl/container/btree_map.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Server {

class GroupedStatsRequest
    : public StatsRequest<std::vector<Stats::TextReadoutSharedPtr>,
                          std::vector<Stats::CounterSharedPtr>, std::vector<Stats::GaugeSharedPtr>,
                          std::vector<Stats::HistogramSharedPtr>> {

public:
  GroupedStatsRequest(Stats::Store& stats, const StatsParams& params,
                      Stats::CustomStatNamespaces& custom_namespaces,
                      UrlHandlerFn url_handler_fn = nullptr);

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

private:
  Stats::CustomStatNamespaces& custom_namespaces_;
  const Stats::SymbolTable& global_symbol_table_;
};

} // namespace Server
} // namespace Envoy
