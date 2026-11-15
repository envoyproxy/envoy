#pragma once

#include "envoy/extensions/http/ai_filters/route_selector/v3/route_selector.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"
#include "source/extensions/http/ai_filters/common/sync_filter.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RouteSelector {

// `success`: a route was selected for the current attempt and applied to headers and filter state.
// `invalid_route_data`: the resolved routes filter state is missing, the upstream attempt count is
// missing or out of bounds, or the selected route has an unspecified LLM protocol.
#define ALL_ROUTE_SELECTOR_FILTER_STATS(COUNTER)                                                   \
  COUNTER(success)                                                                                 \
  COUNTER(invalid_route_data)

struct RouteSelectorFilterStats {
  ALL_ROUTE_SELECTOR_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

// Shared configuration and statistics for the RouteSelector AI filter.
class RouteSelectorFilterConfig {
public:
  RouteSelectorFilterConfig(
      const envoy::extensions::http::ai_filters::route_selector::v3::RouteSelector& proto,
      Stats::Scope& scope);

  const RouteSelectorFilterStats& stats() const { return stats_; }

private:
  const RouteSelectorFilterStats stats_;
};
using RouteSelectorFilterConfigSharedPtr = std::shared_ptr<const RouteSelectorFilterConfig>;

// Upstream AI filter that selects the resolved LLM route for the current upstream attempt.
//
// Reads the candidate `LlmRoute` list published in `envoy.ai.routes`
// (`FilterStateKeys::ResolvedRoutes`, typically by the downstream `model_router` filter) and
// indexes into it using the 1-based upstream `StreamInfo::attemptCount()` so each retry advances to
// the next fallback target.
//
// For the selected route, this filter:
// - Strips existing `authorization`, `x-goog-api-key`, and `x-api-key` request headers, and sets
//   the target route's credential (`x-goog-api-key` for `GEMINI_GENERATE_CONTENT`, `authorization`
//   otherwise) when configured.
// - Updates `:authority` (`Host`) to the route's `host_and_port`.
// - Publishes per-attempt filter state (`FilterChain` lifespan) for downstream upstream AI filters
//   such as `transcoder`:
//   - `envoy.ai.llm_protocol.response` (`FilterStateKeys::LlmProtocolResponse`)
//   - `envoy.ai.model.resolved` (`FilterStateKeys::ModelResolved`)
//   - `envoy.ai.binding.response` (`FilterStateKeys::BindingResponse`)
class RouteSelectorFilter : public Common::SyncAiFilter,
                            public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  RouteSelectorFilter(RouteSelectorFilterConfigSharedPtr config,
                      const HttpFilters::AiProtocolManager::AiFilterContext& context);

  // Common::SyncAiFilter
  absl::Status decodeSync(HttpFilters::AiProtocolManager::AiRequest& request,
                          HttpFilters::AiProtocolManager::LocalReplier reply_locally) override;

private:
  RouteSelectorFilterConfigSharedPtr config_;
  HttpFilters::AiProtocolManager::AiFilterContext context_;
};

} // namespace RouteSelector
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
