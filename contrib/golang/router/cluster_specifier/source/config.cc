#include "contrib/golang/router/cluster_specifier/source/config.h"

#include <chrono>

namespace Envoy {
namespace Router {
namespace Golang {

ClusterSpecifierPluginSharedPtr
GolangClusterSpecifierPluginFactoryConfig::createClusterSpecifierPlugin(
    const Protobuf::Message& config, Server::Configuration::CommonFactoryContext&) {
  const auto& typed_config = dynamic_cast<const GolangClusterProto&>(config);
  auto cluster_config = std::make_shared<ClusterConfig>(typed_config);
  return std::make_shared<GolangClusterSpecifierPlugin>(cluster_config);
}

REGISTER_FACTORY(GolangClusterSpecifierPluginFactoryConfig, ClusterSpecifierPluginFactoryConfig);

} // namespace Golang
} // namespace Router
} // namespace Envoy
