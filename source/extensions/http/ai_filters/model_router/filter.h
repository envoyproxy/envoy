#pragma once

#include "envoy/api/api.h"
#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"
#include "source/extensions/http/ai_filters/common/sync_filter.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

// `success`: the requested model matched a configured route and its target list was published to
// filter state.
// `model_name_missing`: the requested model name was not present in `envoy.ai.model.request` filter
// state.
// `route_not_found`: no route was configured for the requested model name.
#define ALL_MODEL_ROUTER_FILTER_STATS(COUNTER)                                                     \
  COUNTER(success)                                                                                 \
  COUNTER(model_name_missing)                                                                      \
  COUNTER(route_not_found)

struct ModelRouterFilterStats {
  ALL_MODEL_ROUTER_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

// Shared configuration for the ModelRouter AI filter, precomputing the mapping from requested model
// name to the ordered list of resolved `LlmRoute` targets.
class ModelRouterFilterConfig {
public:
  static absl::StatusOr<std::shared_ptr<ModelRouterFilterConfig>>
  create(const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto,
         Api::Api& api, Stats::Scope& scope);

  using ModelMap =
      absl::flat_hash_map<std::string,
                          HttpFilters::AiProtocolManager::LlmRoutesFilterStateObjectSharedPtr>;

  ModelRouterFilterConfig(ModelMap&& routes, Stats::Scope& scope);
  const ModelMap& routes() const { return routes_; }
  const ModelRouterFilterStats& stats() const { return stats_; }

private:
  static absl::StatusOr<ModelMap>
  buildModelMap(const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto,
                Api::Api& api);

  const ModelMap routes_;
  const ModelRouterFilterStats stats_;
};
using ModelRouterFilterConfigSharedPtr = std::shared_ptr<const ModelRouterFilterConfig>;

// Downstream AI filter that resolves a logical or client-requested model name to an ordered list of
// upstream `LlmRoute` targets.
//
// Reads the requested model name from `envoy.ai.model.request` (`FilterStateKeys::ModelRequest`,
// typically populated by `request_info`), looks up the matching route in `ModelRouterFilterConfig`,
// and stores the resolved `LlmRoutesFilterStateObject` under `envoy.ai.routes`
// (`FilterStateKeys::ResolvedRoutes`) with `Request` lifespan so upstream AI filters (such as
// `route_selector`) can select a target per upstream retry attempt.
class ModelRouterFilter : public Common::SyncAiFilter,
                          public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  ModelRouterFilter(ModelRouterFilterConfigSharedPtr config,
                    const HttpFilters::AiProtocolManager::AiFilterContext& context);

  // Common::SyncAiFilter
  absl::Status decodeSync(HttpFilters::AiProtocolManager::AiRequest& request,
                          HttpFilters::AiProtocolManager::LocalReplier reply_locally) override;

private:
  ModelRouterFilterConfigSharedPtr config_;
  HttpFilters::AiProtocolManager::AiFilterContext context_;
};

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
