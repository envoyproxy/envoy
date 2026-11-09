#include "source/extensions/http/ai_filters/route_selector/config.h"

#include <utility>

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/http/ai_filters/route_selector/filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RouteSelector {

using RouteSelectorProto = envoy::extensions::http::ai_filters::route_selector::v3::RouteSelector;

absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
RouteSelectorFilterConfigFactory::createAiFilterFactory(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope) {
  const auto& proto = MessageUtil::downcastAndValidate<const RouteSelectorProto&>(
      config, context.messageValidationVisitor());

  auto filter_config = std::make_shared<const RouteSelectorFilterConfig>(proto, scope);
  return [filter_config](const HttpFilters::AiProtocolManager::AiFilterContext& stream_context)
             -> HttpFilters::AiProtocolManager::AiFilterSharedPtr {
    return std::make_shared<RouteSelectorFilter>(filter_config, stream_context);
  };
}

REGISTER_FACTORY(RouteSelectorFilterConfigFactory,
                 HttpFilters::AiProtocolManager::AiFilterConfigFactory);

} // namespace RouteSelector
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
