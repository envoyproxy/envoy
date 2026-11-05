#include "source/extensions/http/ai_filters/model_router/filter.h"

#include "source/common/config/datasource.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_conversion.h"

#include "absl/status/status_macros.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

namespace FilterStateKeys = HttpFilters::AiProtocolManager::FilterStateKeys;
using HttpFilters::AiProtocolManager::LlmBinding;
using HttpFilters::AiProtocolManager::LlmBindingConstSharedPtr;
using HttpFilters::AiProtocolManager::LlmRoute;
using HttpFilters::AiProtocolManager::LlmRoutesFilterStateObject;
using HttpFilters::AiProtocolManager::protocolFromProto;
using HttpFilters::AiProtocolManager::stringFromFilterState;

absl::StatusOr<std::shared_ptr<ModelRouterFilterConfig>> ModelRouterFilterConfig::create(
    const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto, Api::Api& api,
    Stats::Scope& scope) {
  ABSL_ASSIGN_OR_RETURN(ModelMap model_map, buildModelMap(proto, api));

  return std::make_shared<ModelRouterFilterConfig>(std::move(model_map), scope);
}

ModelRouterFilterConfig::ModelRouterFilterConfig(ModelMap&& routes, Stats::Scope& scope)
    : routes_(std::move(routes)),
      stats_(ModelRouterFilterStats{ALL_MODEL_ROUTER_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "ai_protocol_manager.model_router."))}) {}

absl::StatusOr<ModelRouterFilterConfig::ModelMap> ModelRouterFilterConfig::buildModelMap(
    const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto,
    Api::Api& api) {
  namespace Config = envoy::extensions::http::ai_filters::model_router::v3;

  struct ParsedProvider {
    Config::Provider proto;
    LlmBindingConstSharedPtr binding;
  };

  absl::flat_hash_map<std::string, ParsedProvider> providers;
  for (const Config::Provider& entry : proto.providers()) {
    if (providers.contains(entry.name())) {
      return absl::InvalidArgumentError("Duplicate provider name " + entry.name());
    }
    if (entry.llm_protocol() == envoy::type::ai::v3::LLM_PROTOCOL_UNSPECIFIED) {
      return absl::InvalidArgumentError(
          absl::StrCat("Provider ", entry.name(), " must specify a valid llm_protocol"));
    }
    LlmBindingConstSharedPtr binding;
    if (entry.has_binding()) {
      ABSL_ASSIGN_OR_RETURN(binding, LlmBinding::create(entry.binding()));
    }
    providers.emplace(entry.name(), ParsedProvider{entry, std::move(binding)});
  }

  absl::flat_hash_map<std::string, Config::Model> models;
  for (const Config::Model& entry : proto.models()) {
    if (models.contains(entry.name())) {
      return absl::InvalidArgumentError("Duplicate model name " + entry.name());
    }
    models[entry.name()] = entry;
  }

  absl::flat_hash_map<std::string, std::vector<LlmRoute>> routes_by_model;
  for (const Config::RoutingEntry& entry : proto.routes()) {
    std::vector<LlmRoute>& resolved = routes_by_model[entry.requested_model()];
    std::optional<std::string> route_provider;

    for (const Config::RoutingTarget& target : entry.targets()) {
      auto model_it = models.find(target.model());
      if (model_it == models.end()) {
        return absl::InvalidArgumentError("Unknown model " + target.model());
      }
      auto provider_it = providers.find(model_it->second.provider());
      if (provider_it == providers.end()) {
        return absl::InvalidArgumentError("Unknown provider " + model_it->second.provider());
      }

      // Until routing policies are aware of AI routes, cross-provider fallbacks are not supported.
      if (!route_provider) {
        route_provider = provider_it->first;
      } else if (route_provider != provider_it->first) {
        return absl::InvalidArgumentError("All models in the route must have the same provider");
      }

      LlmRoute route;
      route.llm_protocol = protocolFromProto(provider_it->second.proto.llm_protocol());
      route.model = model_it->second.model();
      route.host_and_port = provider_it->second.proto.host_and_port();
      if (!Http::HeaderUtility::authorityIsValid(route.host_and_port)) {
        return absl::InvalidArgumentError(
            "host_and_port value isn't valid Authority for provider " + provider_it->first);
      }
      route.binding = provider_it->second.binding;
      if (model_it->second.has_api_key()) {
        ABSL_ASSIGN_OR_RETURN(
            std::string api_key,
            Envoy::Config::DataSource::read(model_it->second.api_key(), /*allow_empty=*/true, api));
        route.api_key = std::string(absl::StripAsciiWhitespace(api_key));
        if (!Http::HeaderUtility::headerValueIsValid(route.api_key)) {
          return absl::InvalidArgumentError("API key value isn't valid HTTP header for model " +
                                            route.model);
        }
      }
      resolved.push_back(std::move(route));
    }
  }

  ModelRouterFilterConfig::ModelMap model_map;
  model_map.reserve(routes_by_model.size());
  for (auto& [requested_model, routes] : routes_by_model) {
    model_map.emplace(requested_model,
                      std::make_shared<LlmRoutesFilterStateObject>(std::move(routes)));
  }

  return model_map;
}

ModelRouterFilter::ModelRouterFilter(ModelRouterFilterConfigSharedPtr config,
                                     const HttpFilters::AiProtocolManager::AiFilterContext& context)
    : config_(std::move(config)), context_(context) {}

absl::Status
ModelRouterFilter::decodeSync(HttpFilters::AiProtocolManager::AiRequest&,
                              HttpFilters::AiProtocolManager::LocalReplier reply_locally) {
  const StreamInfo::FilterStateSharedPtr& filter_state = context_.stream_info.filterState();
  // ModelRequest is set by RequestInfo filter.
  auto model_name = stringFromFilterState(*filter_state, FilterStateKeys::ModelRequest);

  if (!model_name) {
    std::move(reply_locally)(Http::Code::NotFound, "Model name not found in filter state");
    config_->stats().model_name_missing_.inc();
    return absl::OkStatus();
  }

  auto it = config_->routes().find(*model_name);
  if (it == config_->routes().end()) {
    std::move(reply_locally)(Http::Code::NotFound, "Unknown model");
    config_->stats().route_not_found_.inc();
    return absl::OkStatus();
  }

  filter_state->setData(FilterStateKeys::ResolvedRoutes, it->second,
                        StreamInfo::FilterState::LifeSpan::Request);

  config_->stats().success_.inc();

  // Temporary fix to support DFP cluster, until the router and DFP filter support ResolvedRoutes.
  // Assumes that all routes point to the same host. There is no such requirement with STATIC_DNS
  // clusters.
  if (!it->second->routes().empty()) {
    Envoy::Http::Utility::updateAuthority(context_.request_headers,
                                          it->second->routes()[0].host_and_port, false, false);
    context_.request_headers.setEnvoyMaxRetries(it->second->routes().size() - 1);
  }

  return absl::OkStatus();
}

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
