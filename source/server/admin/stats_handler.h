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
   public:
    Context(Server::Instance& server,
            bool used_only, absl::optional<std::regex> regex,
            bool json, Http::ResponseHeaderMap& response_headers,
            Buffer::Instance& response);
    ~Context();

    bool nextChunk();
    Http::Code writeChunk(Buffer::Instance& response);

    template<class StatType> bool shouldShowMetric(const StatType& stat) {
      return StatsHandler::shouldShowMetric(stat, used_only_, regex_);
    }

    Http::Code statsAsText(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response);
    Http::Code statsAsJson(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                           bool pretty);

    const bool used_only_;
    absl::optional<std::regex> regex_;
    absl::optional<std::string> format_value_;

    std::unique_ptr<Render> render_;

    using StatOrScope = absl::variant<
        Stats::ScopeSharedPtr,
        Stats::TextReadoutSharedPtr,
        Stats::CounterSharedPtr,
        Stats::GaugeSharedPtr,
        Stats::ParentHistogramSharedPtr>;
    using StatOrScopeVec = std::vector<StatOrScope>;

    static constexpr uint32_t chunk_size_ = 10;
    Stats::Store& stats_;
    StatOrScopeVec stats_and_scopes_;
    uint32_t stats_and_scopes_index_{0};
    uint32_t chunk_index_{0};
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
