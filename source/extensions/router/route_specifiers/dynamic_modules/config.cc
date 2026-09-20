#include "source/extensions/router/route_specifiers/dynamic_modules/config.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace RouteSpecifiers {
namespace DynamicModules {

absl::StatusOr<Envoy::Router::RouteSpecifierSharedPtr>
DynamicModuleRouteSpecifierFactory::createRouteSpecifier(
    const Protobuf::Message& config, Envoy::Router::RouteSpecifierFactoryContext& context) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const DynamicModuleRouteSpecifierProto&>(
          config, context.serverFactoryContext().messageValidationVisitor());

  // No init manager is passed, so a remote source that is not already cached on disk cannot be
  // awaited and is rejected by newDynamicModuleByConfig.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), proto_config.stat_prefix(),
      context.serverFactoryContext());
  RETURN_IF_NOT_OK_REF(load_result.status());

  auto specifier_config =
      newDynamicModuleRouteSpecifierConfig(proto_config, std::move(load_result->loaded), context);
  if (!specifier_config.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        context.serverFactoryContext(), proto_config.stat_prefix(),
        Extensions::DynamicModules::ConfigInitErrorStat);
    return absl::InvalidArgumentError(absl::StrCat("Failed to create route specifier config: ",
                                                   specifier_config.status().message()));
  }

  return std::make_shared<DynamicModuleRouteSpecifier>(std::move(specifier_config.value()));
}

REGISTER_FACTORY(DynamicModuleRouteSpecifierFactory, Envoy::Router::RouteSpecifierFactory);

} // namespace DynamicModules
} // namespace RouteSpecifiers
} // namespace Extensions
} // namespace Envoy
