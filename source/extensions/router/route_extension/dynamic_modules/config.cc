#include "source/extensions/router/route_extension/dynamic_modules/config.h"

#include "envoy/extensions/router/route_extension/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {

absl::StatusOr<Envoy::Router::RouteExtensionSharedPtr>
DynamicModuleRouteExtensionFactory::createRouteExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const DynamicModuleRouteExtensionProto&>(
          config, context.messageValidationVisitor());

  const auto& extension_name = proto_config.extension_name();
  // No init manager is passed, so a remote source that is not already cached on disk cannot be
  // awaited and is rejected by newDynamicModuleByConfig.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), extension_name, context);
  RETURN_IF_NOT_OK_REF(load_result.status());

  auto extension_config =
      newDynamicModuleRouteExtensionConfig(proto_config, std::move(load_result->loaded), context);
  if (!extension_config.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        context, extension_name, Extensions::DynamicModules::ConfigInitErrorStat);
    return extension_config.status();
  }

  return std::make_shared<DynamicModuleRouteExtension>(std::move(extension_config.value()));
}

REGISTER_FACTORY(DynamicModuleRouteExtensionFactory, Envoy::Router::RouteExtensionFactory);

} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy
