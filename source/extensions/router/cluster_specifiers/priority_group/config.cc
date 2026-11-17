#include "source/extensions/router/cluster_specifiers/priority_group/config.h"

#include "envoy/extensions/router/cluster_specifiers/priority_group/v3/priority_group.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace PriorityGroup {

Envoy::Router::ClusterSpecifierPluginSharedPtr
PriorityGroupClusterSpecifierPluginFactoryConfig::createClusterSpecifierPlugin(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& typed_config =
      MessageUtil::downcastAndValidate<const PriorityGroupClusterSpecifierConfigProto&>(
          config, context.messageValidationVisitor());

  return std::make_shared<PriorityGroupClusterSpecifierPlugin>(typed_config);
}

REGISTER_FACTORY(PriorityGroupClusterSpecifierPluginFactoryConfig,
                 Envoy::Router::ClusterSpecifierPluginFactoryConfig);

} // namespace PriorityGroup
} // namespace Router
} // namespace Extensions
} // namespace Envoy
