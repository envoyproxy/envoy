#pragma once

#include <variant>
#include <vector>

#include "envoy/server/admin.h"

#include "source/server/admin/stats_params.h"
#include "source/server/admin/stats_render.h"
#include "source/server/admin/utils.h"

#include "absl/container/btree_map.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Server {

template <class TR, class C, class G, class H> class StatsRequestBase : public Admin::Request {
protected:
  enum class StatOrScopesIndex { Scopes, TextReadout, Counter, Gauge, Histogram };

  enum class PhaseName {
    TextReadouts,
    CountersAndGauges,
    Counters,
    Gauges,
    Histograms,
  };

  struct Phase {
  public:
    PhaseName phase;
    std::string phase_label;
  };

public:
  using ScopeVec = std::vector<Stats::ConstScopeSharedPtr>;

  using StatOrScopes = absl::variant<ScopeVec, TR, C, G, H>;

  using UrlHandlerFn = std::function<Admin::UrlHandler()>;

  static constexpr uint64_t DefaultChunkSize = 2 * 1000 * 1000;

  StatsRequestBase(Stats::Store& stats, const StatsParams& params,
                   UrlHandlerFn url_handler_fn = nullptr);

  virtual ~StatsRequestBase() = default;

  // Admin::Request
  Http::Code start(Http::ResponseHeaderMap& response_headers) override;

  // Admin::Request
  bool nextChunk(Buffer::Instance& response) override;

  void startPhase();

  void populateStatsForCurrentPhase(const ScopeVec& scope_vec);

  template <class StatType> void populateStatsFromScopes(const ScopeVec& scope_vec);

  template <class SharedStatType>
  void renderStat(const std::string& name, Buffer::Instance& response, const StatOrScopes& variant);

  virtual void handleTextReadout(Buffer::Instance& response, const StatOrScopes& variant) PURE;

  virtual void handleGauge(Buffer::Instance& response, const StatOrScopes& variant) PURE;

  virtual void handleCounter(Buffer::Instance& response, const StatOrScopes& variant) PURE;

  virtual void handleHistogram(Buffer::Instance& response, const StatOrScopes& variant) PURE;

  void setChunkSize(uint64_t chunk_size) { chunk_size_ = chunk_size; }

protected:
  StatsParams params_;
  Stats::Store& stats_;
  ScopeVec scopes_;
  absl::btree_map<std::string, StatOrScopes> stat_map_;
  std::unique_ptr<StatsRender> render_;
  Buffer::OwnedImpl response_;
  UrlHandlerFn url_handler_fn_;
  uint64_t chunk_size_{DefaultChunkSize};

  // phase-related state
  uint64_t phase_stat_count_{0};
  unsigned short phase_;
  std::vector<Phase> phases_;
};

} // namespace Server
} // namespace Envoy
