#pragma once

#include "source/extensions/router/cluster_specifiers/priority_group/priority_group_cluster_specifier.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace PriorityGroup {

class PriorityGroupClusterSpecifierPluginFactoryConfig
    : public Envoy::Router::ClusterSpecifierPluginFactoryConfig {
public:
  PriorityGroupClusterSpecifierPluginFactoryConfig() = default;

  /**
   * Creates a priority group based cluster specifier plugin.
   * @param config the priority group cluster specifier configuration.
   * @param context the factory context for accessing cluster manager and other services.
   * @return shared pointer to the created cluster specifier plugin.
   */
  Envoy::Router::ClusterSpecifierPluginSharedPtr
  createClusterSpecifierPlugin(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<PriorityGroupClusterSpecifierConfigProto>();
  }

  std::string name() const override {
    return "envoy.router.cluster_specifier_plugin.priority_group";
  }
};

} // namespace PriorityGroup
} // namespace Router
} // namespace Extensions
} // namespace Envoy
