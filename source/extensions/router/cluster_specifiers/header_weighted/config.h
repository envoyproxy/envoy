#pragma once

#include "source/extensions/router/cluster_specifiers/header_weighted/header_weighted.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace HeaderWeighted {

class HeaderWeightedClusterSpecifierPluginFactoryConfig
    : public Envoy::Router::ClusterSpecifierPluginFactoryConfig {
public:
  HeaderWeightedClusterSpecifierPluginFactoryConfig() = default;

  Envoy::Router::ClusterSpecifierPluginSharedPtr
  createClusterSpecifierPlugin(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext&) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<HeaderWeightedConfigProto>();
  }

  std::string name() const override {
    return "envoy.router.cluster_specifier_plugin.header_weighted";
  }
};

} // namespace HeaderWeighted
} // namespace Router
} // namespace Extensions
} // namespace Envoy
