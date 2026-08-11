#pragma once

#include "source/extensions/router/cluster_specifiers/weighted_priority/weighted_priority_cluster_specifier.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace WeightedPriority {

class WeightedPriorityClusterSpecifierPluginFactoryConfig
    : public Envoy::Router::ClusterSpecifierPluginFactoryConfig {
public:
  WeightedPriorityClusterSpecifierPluginFactoryConfig() = default;

  Envoy::Router::ClusterSpecifierPluginSharedPtr
  createClusterSpecifierPlugin(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<WeightedPriorityClusterSpecifierConfigProto>();
  }

  std::string name() const override {
    return "envoy.router.cluster_specifier_plugin.weighted_priority";
  }
};

} // namespace WeightedPriority
} // namespace Router
} // namespace Extensions
} // namespace Envoy
