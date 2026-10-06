#include "source/extensions/router/cluster_specifiers/dynamic_modules/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/router/cluster_specifiers/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {

Envoy::Router::ClusterSpecifierPluginSharedPtr
DynamicModuleClusterSpecifierPluginFactoryConfig::createClusterSpecifierPlugin(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const DynamicModuleClusterSpecifierProto&>(
          config, context.messageValidationVisitor());

  const auto& specifier_name = proto_config.specifier_name();
  // No init manager is passed, so a remote source that is not already cached on disk cannot be
  // awaited and is rejected by newDynamicModuleByConfig.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), specifier_name, context);
  if (!load_result.ok()) {
    throw EnvoyException(std::string(load_result.status().message()));
  }

  auto specifier_config =
      newDynamicModuleClusterSpecifierConfig(proto_config, std::move(load_result->loaded), context);
  if (!specifier_config.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        context, specifier_name, Extensions::DynamicModules::ConfigInitErrorStat);
    throw EnvoyException("Failed to create cluster specifier config: " +
                         std::string(specifier_config.status().message()));
  }

  return std::make_shared<DynamicModuleClusterSpecifierPlugin>(std::move(specifier_config.value()));
}

REGISTER_FACTORY(DynamicModuleClusterSpecifierPluginFactoryConfig,
                 Envoy::Router::ClusterSpecifierPluginFactoryConfig);

} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy
