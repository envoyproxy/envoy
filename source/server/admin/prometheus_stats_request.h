#pragma once

#include <vector>

#include "envoy/server/admin.h"

#include "source/server/admin/stats_params.h"
#include "source/server/admin/stats_render.h"
#include "source/server/admin/utils.h"

#include "absl/container/btree_map.h"
#include "absl/types/variant.h"
#include "prometheus_stats_render.h"

namespace Envoy {
namespace Server {

// Captures context for a streaming request, implementing the AdminHandler interface.
// FIXME for now I am having a separate class altogether, with a lot of duplication
// that can possibly be factored out
class PrometheusStatsRequest : public Admin::Request {
  using ScopeVec = std::vector<Stats::ConstScopeSharedPtr>;
  using TextReadoutStatVec = std::vector<Stats::TextReadoutSharedPtr>;
  using CounterStatVec = std::vector<Stats::CounterSharedPtr>;
  using GaugeStatVec = std::vector<Stats::GaugeSharedPtr>;
  using HistogramStatVec = std::vector<Stats::HistogramSharedPtr>;

  using StatOrScopes =
      absl::variant<ScopeVec, TextReadoutStatVec, CounterStatVec, GaugeStatVec, HistogramStatVec>;

  enum class StatOrScopesIndex { Scopes, TextReadout, Counter, Gauge, Histogram };

  enum class Phase {
    TextReadouts,
    CountersAndGauges,
    Histograms,
  };

public:
  using UrlHandlerFn = std::function<Admin::UrlHandler()>;

  static constexpr uint64_t DefaultChunkSize = 2 * 1000 * 1000;

  PrometheusStatsRequest(Stats::Store& stats, const StatsParams& params,
                         Stats::CustomStatNamespaces& custom_namespaces,
                         UrlHandlerFn url_handler_fn = nullptr);

  // Admin::Request
  Http::Code start(Http::ResponseHeaderMap& response_headers) override;

  bool nextChunk(Buffer::Instance& response) override;

  void startPhase();

  void populateStatsForCurrentPhase(const ScopeVec& scope_vec);

  template <class StatType> void populateStatsFromScopes(const ScopeVec& scope_vec);

  template <class SharedStatType>
  void renderStat(const std::string& name, Buffer::Instance& response, StatOrScopes& variant);

  void setChunkSize(uint64_t chunk_size) { chunk_size_ = chunk_size; }

  template <class SharedStatType>
  absl::optional<std::string> prefixedTagExtractedName(const StatOrScopes& variant);

private:
  StatsParams params_;
  std::unique_ptr<PrometheusStatsRender> render_ = std::make_unique<PrometheusStatsRender>();
  Stats::Store& stats_;
  ScopeVec scopes_;
  absl::btree_map<std::string, StatOrScopes> stat_map_;
  Phase phase_{Phase::CountersAndGauges};
  uint64_t phase_stat_count_{0};
  absl::string_view phase_string_{"Counters and Gauges"};
  Buffer::OwnedImpl response_;
  UrlHandlerFn url_handler_fn_;
  uint64_t chunk_size_{DefaultChunkSize};
  Stats::CustomStatNamespaces& custom_namespaces_;
};

} // namespace Server
} // namespace Envoy
