#include "source/extensions/http/ai_filters/model_router/config.h"

#include <utility>

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/http/ai_filters/model_router/filter.h"

#include "absl/status/status_macros.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

using ModelRouterProto = envoy::extensions::http::ai_filters::model_router::v3::ModelRouter;

absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
ModelRouterFilterConfigFactory::createAiFilterFactory(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope) {
  const auto& proto = MessageUtil::downcastAndValidate<const ModelRouterProto&>(
      config, context.messageValidationVisitor());

  ModelRouterFilterConfigSharedPtr filter_config;
  ABSL_ASSIGN_OR_RETURN(filter_config, ModelRouterFilterConfig::create(proto, scope));

  return [filter_config](const HttpFilters::AiProtocolManager::AiFilterContext& stream_context)
             -> HttpFilters::AiProtocolManager::AiFilterSharedPtr {
    return std::make_shared<ModelRouterFilter>(filter_config, stream_context);
  };
}

REGISTER_FACTORY(ModelRouterFilterConfigFactory,
                 HttpFilters::AiProtocolManager::AiFilterConfigFactory);

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
