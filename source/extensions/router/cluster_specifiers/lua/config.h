#pragma once

#include "source/extensions/router/cluster_specifiers/lua/lua_cluster_specifier.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Lua {

class LuaClusterSpecifierPluginFactoryConfig
    : public Envoy::Router::ClusterSpecifierPluginFactoryConfig {
public:
  LuaClusterSpecifierPluginFactoryConfig() = default;
  Envoy::Router::ClusterSpecifierPluginSharedPtr
  createClusterSpecifierPlugin(const Protobuf::Message& config,
                               Server::Configuration::CommonFactoryContext&) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<LuaClusterSpecifierConfigProto>();
  }

  std::string name() const override { return "envoy.router.cluster_specifier_plugin.lua"; }
};

} // namespace Lua
} // namespace Router
} // namespace Extensions
} // namespace Envoy
