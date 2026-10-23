#pragma once

#include "envoy/extensions/http/ai_filters/route_selector/v3/route_selector.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/type/ai/v3/routes.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"
#include "source/extensions/http/ai_filters/common/sync_filter.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RouteSelector {

#define ALL_ROUTE_SELECTOR_FILTER_STATS(COUNTER)                                                   \
  COUNTER(success)                                                                                 \
  COUNTER(invalid_route_data)

struct RouteSelectorFilterStats {
  ALL_ROUTE_SELECTOR_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

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

class RouteSelectorFilter : public Common::SyncAiFilter,
                            public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  RouteSelectorFilter(RouteSelectorFilterConfigSharedPtr config,
                      const HttpFilters::AiProtocolManager::AiFilterContext& context);

  // Common::SyncAiFilter
  absl::Status decodeSync(HttpFilters::AiProtocolManager::AiRequest& request,
                          HttpFilters::AiProtocolManager::LocalReplier reply_locally) override;

private:
  static absl::StatusOr<std::shared_ptr<HttpFilters::AiProtocolManager::LlmBindingObject>>
  createBinding(const envoy::type::ai::v3::LLMBinding& binding);

  RouteSelectorFilterConfigSharedPtr config_;
  HttpFilters::AiProtocolManager::AiFilterContext context_;
};

} // namespace RouteSelector
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
