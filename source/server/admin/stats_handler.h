#pragma once

#include <regex>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/common/stats/histogram_impl.h"
#include "source/server/admin/handler_ctx.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Server {

class StatsHandler : public HandlerContextBase {

public:
  StatsHandler(Server::Instance& server);

  Http::Code handlerResetCounters(absl::string_view path_and_query,
                                  Http::ResponseHeaderMap& response_headers,
                                  Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookups(absl::string_view path_and_query,
                                       Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookupsClear(absl::string_view path_and_query,
                                            Http::ResponseHeaderMap& response_headers,
                                            Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookupsDisable(absl::string_view path_and_query,
                                              Http::ResponseHeaderMap& response_headers,
                                              Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookupsEnable(absl::string_view path_and_query,
                                             Http::ResponseHeaderMap& response_headers,
                                             Buffer::Instance& response, AdminStream&);
  Http::Code handlerStats(absl::string_view path_and_query,
                          Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                          AdminStream&);
  Http::Code handlerPrometheusStats(absl::string_view path_and_query,
                                    Http::ResponseHeaderMap& response_headers,
                                    Buffer::Instance& response, AdminStream&);
  Http::Code handlerContention(absl::string_view path_and_query,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&);

  class JsonRender;
  class Render;
  class TextRender;

  class Context {
    using ScopeVec = std::vector<Stats::ConstScopeSharedPtr>;
    using StatOrScopes =
        absl::variant<ScopeVec, Stats::TextReadoutSharedPtr, Stats::CounterSharedPtr,
                      Stats::GaugeSharedPtr, Stats::HistogramSharedPtr>;
    enum class Phase {
      TextReadouts,
      CountersAndGauges,
      Histograms,
    };

  public:
    Context(Server::Instance& server, bool used_only, absl::optional<std::regex> regex, bool json,
            Http::ResponseHeaderMap& response_headers, Buffer::Instance& response);
    ~Context();

    void startPhase();
    Http::Code writeChunk(Buffer::Instance& response);

    template <class StatType> bool shouldShowMetric(const StatType& stat) {
      return StatsHandler::shouldShowMetric(stat, used_only_, regex_);
    }

    void populateStatsForCurrentPhase(const ScopeVec& scope_vec);
    template <class StatType> void populateStatsFromScopes(const ScopeVec& scope);
    template <class SharedStatType>
    void renderStat(const std::string& name, Buffer::Instance& response, StatOrScopes& variant);
    template <class SharedStatType> bool skip(const SharedStatType& stat, const std::string& name);
    const bool used_only_;
    absl::optional<std::regex> regex_;
    absl::optional<std::string> format_value_;

    std::unique_ptr<Render> render_;

    static constexpr uint32_t num_stats_per_chunk_ = 1000;
    Stats::Store& stats_;
    ScopeVec scopes_;
    // StatOrScopeVec stats_and_scopes_;
    using StatMap = std::map<std::string, StatOrScopes>;
    StatMap stat_map_;
    uint32_t stats_and_scopes_index_{0};
    uint32_t chunk_index_{0};
    Phase phase_{Phase::TextReadouts};
  };
  using ContextPtr = std::unique_ptr<Context>;

private:
  template <class StatType>
  static bool shouldShowMetric(const StatType& metric, const bool used_only,
                               const absl::optional<std::regex>& regex) {
    return ((!used_only || metric.used()) &&
            (!regex.has_value() || std::regex_search(metric.name(), regex.value())));
  }

  friend class StatsHandlerTest;

  absl::flat_hash_map<AdminStream*, std::unique_ptr<Context>> context_map_;
};

} // namespace Server
} // namespace Envoy
