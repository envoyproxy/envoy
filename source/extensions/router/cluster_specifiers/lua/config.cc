#include "source/extensions/router/cluster_specifiers/lua/config.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Lua {

Envoy::Router::ClusterSpecifierPluginSharedPtr
LuaClusterSpecifierPluginFactoryConfig::createClusterSpecifierPlugin(
    const Protobuf::Message& config, Server::Configuration::CommonFactoryContext& context) {

  const auto& typed_config = dynamic_cast<const LuaClusterSpecifierConfigProto&>(config);
  auto cluster_config = std::make_shared<LuaClusterSpecifierConfig>(typed_config, context);
  return std::make_shared<LuaClusterSpecifierPlugin>(cluster_config);
}

REGISTER_FACTORY(LuaClusterSpecifierPluginFactoryConfig,
                 Envoy::Router::ClusterSpecifierPluginFactoryConfig);

} // namespace Lua
} // namespace Router
} // namespace Extensions
} // namespace Envoy
