#include "source/extensions/router/cluster_specifiers/header_weighted/config.h"

#include "envoy/extensions/router/cluster_specifiers/header_weighted/v3/header_weighted.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace HeaderWeighted {

Envoy::Router::ClusterSpecifierPluginSharedPtr
HeaderWeightedClusterSpecifierPluginFactoryConfig::createClusterSpecifierPlugin(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& typed_config = MessageUtil::downcastAndValidate<const HeaderWeightedConfigProto&>(
      config, context.messageValidationVisitor());
  return std::make_shared<HeaderWeightedClusterSpecifierPlugin>(typed_config);
}

REGISTER_FACTORY(HeaderWeightedClusterSpecifierPluginFactoryConfig,
                 Envoy::Router::ClusterSpecifierPluginFactoryConfig);

} // namespace HeaderWeighted
} // namespace Router
} // namespace Extensions
} // namespace Envoy
