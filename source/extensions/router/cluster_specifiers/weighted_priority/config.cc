#include "source/extensions/router/cluster_specifiers/weighted_priority/config.h"

#include "envoy/extensions/router/cluster_specifiers/weighted_priority/v3/weighted_priority.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace WeightedPriority {

Envoy::Router::ClusterSpecifierPluginSharedPtr
WeightedPriorityClusterSpecifierPluginFactoryConfig::createClusterSpecifierPlugin(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& typed_config =
      MessageUtil::downcastAndValidate<const WeightedPriorityClusterSpecifierConfigProto&>(
          config, context.messageValidationVisitor());
  return std::make_shared<WeightedPriorityClusterSpecifierPlugin>(typed_config,
                                                                  context.clusterManager());
}

REGISTER_FACTORY(WeightedPriorityClusterSpecifierPluginFactoryConfig,
                 Envoy::Router::ClusterSpecifierPluginFactoryConfig);

} // namespace WeightedPriority
} // namespace Router
} // namespace Extensions
} // namespace Envoy
