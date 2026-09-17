#pragma once

#include "source/extensions/router/cluster_specifiers/dynamic_modules/cluster_specifier.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {

class DynamicModuleClusterSpecifierPluginFactoryConfig
    : public Envoy::Router::ClusterSpecifierPluginFactoryConfig {
public:
  // Router::ClusterSpecifierPluginFactoryConfig
  Envoy::Router::ClusterSpecifierPluginSharedPtr
  createClusterSpecifierPlugin(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<DynamicModuleClusterSpecifierProto>();
  }

  std::string name() const override {
    return "envoy.router.cluster_specifier_plugin.dynamic_modules";
  }
};

} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy
