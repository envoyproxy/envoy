#include "source/extensions/http/ai_filters/model_router/filter.h"

#include "source/common/http/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"
#include "source/extensions/http/ai_filters/model_router/route_wrapper.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

namespace FilterStateKeys = HttpFilters::AiProtocolManager::FilterStateKeys;
using HttpFilters::AiProtocolManager::stringFromFilterState;

absl::StatusOr<std::shared_ptr<ModelRouterFilterConfig>> ModelRouterFilterConfig::create(
    const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto,
    Stats::Scope& scope) {
  absl::StatusOr<ModelMap> model_map = buildModelMap(proto);

  if (!model_map.ok()) {
    return model_map.status();
  }

  return std::make_shared<ModelRouterFilterConfig>(std::move(model_map).value(), scope);
}

ModelRouterFilterConfig::ModelRouterFilterConfig(ModelMap&& routes, Stats::Scope& scope)
    : routes_(std::move(routes)),
      stats_(ModelRouterFilterStats{ALL_MODEL_ROUTER_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "ai_protocol_manager.model_router."))}) {}

absl::StatusOr<ModelRouterFilterConfig::ModelMap> ModelRouterFilterConfig::buildModelMap(
    const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto) {
  namespace Config = envoy::extensions::http::ai_filters::model_router::v3;

  absl::flat_hash_map<std::string, Config::Provider> providers;
  for (const Config::Provider& entry : proto.providers()) {
    providers[entry.name()] = entry;
  }

  absl::flat_hash_map<std::string, Config::Model> models;
  for (const Config::Model& entry : proto.models()) {
    models[entry.name()] = entry;
  }

  ModelRouterFilterConfig::ModelMap model_map;
  for (const Config::RoutingEntry& entry : proto.routes()) {
    envoy::data::ai::v3::LlmRoutes& routes = model_map[entry.requested_model()];

    for (const Config::RoutingTarget& target : entry.targets()) {
      envoy::data::ai::v3::LlmRoute* route = routes.add_routes();

      auto model_it = models.find(target.model());
      if (model_it == models.end()) {
        return absl::InvalidArgumentError("Unknown model " + target.model());
      }
      auto provider_it = providers.find(model_it->second.provider());
      if (provider_it == providers.end()) {
        return absl::InvalidArgumentError("Unknown provider " + model_it->second.provider());
      }

      route->set_llm_protocol(provider_it->second.llm_protocol());
      route->set_model(model_it->second.model());
      route->set_host_and_port(provider_it->second.host_and_port());
      route->set_api_key(model_it->second.api_key());

      if (provider_it->second.has_binding()) {
        *route->mutable_binding() = provider_it->second.binding();
      }
    }
  }

  return model_map;
}

ModelRouterFilter::ModelRouterFilter(ModelRouterFilterConfigSharedPtr config,
                                     const HttpFilters::AiProtocolManager::AiFilterContext& context)
    : config_(std::move(config)), context_(context) {}

absl::Status ModelRouterFilter::decodeSync(HttpFilters::AiProtocolManager::AiRequest&,
                                           HttpFilters::AiProtocolManager::LocalReplier) {
  const StreamInfo::FilterStateSharedPtr& filter_state = context_.stream_info.filterState();
  auto model_name = stringFromFilterState(*filter_state, FilterStateKeys::ModelRequest);

  if (!model_name) {
    config_->stats().model_name_missing_.inc();
    return absl::OkStatus();
  }

  auto it = config_->routes().find(*model_name);
  if (it == config_->routes().end()) {
    config_->stats().route_not_found_.inc();
    return absl::OkStatus();
  }

  filter_state->setData(FilterStateKeys::ResolvedRoutes,
                        std::make_shared<LlmRoutesFilterStateObject>(it->second),
                        StreamInfo::FilterState::LifeSpan::Request,
                        StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

  config_->stats().success_.inc();

  // Temporary fix, until the router and DFP filter support ResolvedRoutes.
  // Assumes that all routes point to the same host.
  if (!it->second.routes().empty()) {
    Envoy::Http::Utility::updateAuthority(context_.request_headers,
                                          it->second.routes(0).host_and_port(), false, false);
    context_.request_headers.setEnvoyMaxRetries(it->second.routes().size() - 1);
  }

  return absl::OkStatus();
}

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
