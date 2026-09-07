#include "source/extensions/router/route_providers/dynamic_modules/config.h"

#include <utility>
#include <vector>

#include "envoy/extensions/router/route_providers/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace RouteProviders {
namespace DynamicModules {

absl::StatusOr<Envoy::Router::RouteProducerSharedPtr>
DynamicModuleRouteProviderFactory::createRouteProvider(
    const Protobuf::Message& config,
    const std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr>& route_templates,
    Server::Configuration::ServerFactoryContext& context, Init::Manager& init_manager) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const DynamicModuleRouteProviderProto&>(
          config, context.messageValidationVisitor());

  const auto& provider_name = proto_config.provider_name();
  // No init manager is passed to the module loader, so a remote source that is not already cached
  // on disk cannot be awaited and is rejected by newDynamicModuleByConfig.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), provider_name, context);
  RETURN_IF_NOT_OK_REF(load_result.status());

  auto provider_config = newDynamicModuleRouteProviderConfig(
      proto_config, std::move(load_result->loaded),
      std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr>(route_templates), context,
      init_manager);
  if (!provider_config.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        context, provider_name, Extensions::DynamicModules::ConfigInitErrorStat);
    return provider_config.status();
  }

  return std::make_shared<DynamicModuleRouteProvider>(std::move(provider_config.value()));
}

REGISTER_FACTORY(DynamicModuleRouteProviderFactory, Envoy::Router::RouteProviderFactory);

} // namespace DynamicModules
} // namespace RouteProviders
} // namespace Router
} // namespace Extensions
} // namespace Envoy
