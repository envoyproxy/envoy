#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "envoy/api/api.h"
#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"
#include "source/extensions/http/ai_filters/model_router/quota_interface.h"

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
// `quota_exceeded`: all candidate models in the matched route exceeded their configured quotas.
#define ALL_MODEL_ROUTER_FILTER_STATS(COUNTER)                                                     \
  COUNTER(success)                                                                                 \
  COUNTER(model_name_missing)                                                                      \
  COUNTER(route_not_found)                                                                         \
  COUNTER(quota_exceeded)

struct ModelRouterFilterStats {
  ALL_MODEL_ROUTER_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

// Shared configuration for the ModelRouter AI filter, precomputing the mapping from requested model
// name to the ordered list of resolved `LlmRoute` targets.
class ModelRouterFilterConfig {
public:
  using QuotaPtr = ThreadLocal::TypedSlotPtr<QuotaInterface>;

  static absl::StatusOr<std::shared_ptr<ModelRouterFilterConfig>>
  create(const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto,
         Api::Api& api, Stats::Scope& scope);

  using ModelMap =
      absl::flat_hash_map<std::string,
                          HttpFilters::AiProtocolManager::LlmRoutesFilterStateObjectSharedPtr>;

  ModelRouterFilterConfig(ModelMap&& routes, Stats::Scope& scope);
  const ModelMap& routes() const { return routes_; }
  const ModelRouterFilterStats& stats() const { return stats_; }
  QuotaInterface* quotas() const {
    if (!tl_quota_) {
      return nullptr;
    }

    return tl_quota_->get().ptr();
  }
  void setQuotas(QuotaPtr tl_quota) { tl_quota_ = std::move(tl_quota); }

private:
  static absl::StatusOr<ModelMap>
  buildModelMap(const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto,
                Api::Api& api);

  const ModelMap routes_;
  const ModelRouterFilterStats stats_;
  QuotaPtr tl_quota_;
};
using ModelRouterFilterConfigSharedPtr = std::shared_ptr<const ModelRouterFilterConfig>;

// Downstream AI filter that resolves a logical or client-requested model name to an ordered list of
// upstream `LlmRoute` targets.
//
// Reads the requested model name from `envoy.ai.model.request` (`FilterStateKeys::ModelRequest`,
// typically populated by `request_info`), looks up the matching route in `ModelRouterFilterConfig`,
// filters out candidate models that have exceeded their quotas, and stores the resolved
// `LlmRoutesFilterStateObject` under `envoy.ai.routes` (`FilterStateKeys::ResolvedRoutes`) with
// `Request` lifespan so upstream AI filters (such as `route_selector`) can select a target per
// upstream retry attempt.
class ModelRouterFilter : public HttpFilters::AiProtocolManager::AiFilter,
                          public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  ModelRouterFilter(ModelRouterFilterConfigSharedPtr config,
                    const HttpFilters::AiProtocolManager::AiFilterContext& context);

  // HttpFilters::AiProtocolManager::AiFilter
  Coroutine::Task<absl::Status>
  decode(HttpFilters::AiProtocolManager::AiRequestReceiver receive_request,
         HttpFilters::AiProtocolManager::AiRequestPropagator propagate_request,
         HttpFilters::AiProtocolManager::LocalReplier reply_locally) override;

  Coroutine::Task<absl::Status>
  encodeSSE(HttpFilters::AiProtocolManager::SseStreamReceiver receive_sse,
            HttpFilters::AiProtocolManager::SseStreamPropagator propagate_sse) override;

  Coroutine::Task<absl::Status> encodeUnary(
      HttpFilters::AiProtocolManager::AiResponseStreamReceiver receive_response,
      HttpFilters::AiProtocolManager::AiResponseStreamPropagator propagate_response) override;

private:
  uint64_t getEstimatedInputTokens() const;
  Coroutine::Task<
      absl::StatusOr<HttpFilters::AiProtocolManager::LlmRoutesFilterStateObjectSharedPtr>>
  filterRoutesByQuota(const HttpFilters::AiProtocolManager::LlmRoutesFilterStateObjectSharedPtr&
                          configured_routes) const;
  bool shouldConsumeQuota() const;
  Coroutine::Task<absl::Status> consumeQuota() const;

  ModelRouterFilterConfigSharedPtr config_;
  HttpFilters::AiProtocolManager::AiFilterContext context_;
};

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
