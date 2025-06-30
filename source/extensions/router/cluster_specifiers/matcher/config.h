#pragma once

#include "source/extensions/router/cluster_specifiers/matcher/matcher_cluster_specifier.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Matcher {

class MatcherClusterSpecifierPluginFactoryConfig
    : public Envoy::Router::ClusterSpecifierPluginFactoryConfig {
public:
  MatcherClusterSpecifierPluginFactoryConfig() = default;
  /**
   * Creates a matcher-based cluster specifier plugin.
   * @param config the matcher cluster specifier configuration
   * @param context the factory context for accessing cluster manager and other services
   * @return shared pointer to the created cluster specifier plugin
   */
  Envoy::Router::ClusterSpecifierPluginSharedPtr
  createClusterSpecifierPlugin(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext&) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<MatcherClusterSpecifierConfigProto>();
  }

  std::string name() const override { return "envoy.router.cluster_specifier_plugin.matcher"; }
};

} // namespace Matcher
} // namespace Router
} // namespace Extensions
} // namespace Envoy
