#include "source/extensions/router/cluster_specifiers/matcher/config.h"

#include "envoy/extensions/router/cluster_specifiers/matcher/v3/matcher.pb.validate.h"

#include "source/common/router/matcher_visitor.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Matcher {

Envoy::Router::ClusterSpecifierPluginSharedPtr
MatcherClusterSpecifierPluginFactoryConfig::createClusterSpecifierPlugin(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& typed_config =
      MessageUtil::downcastAndValidate<const MatcherClusterSpecifierConfigProto&>(
          config, context.messageValidationVisitor());

  ClusterActionContext action_context;
  // Reuse the validation visitor because the new cluster specifier matcher has same input
  // with route matcher.
  Envoy::Router::RouteActionValidationVisitor validation_visitor;
  Envoy::Matcher::MatchTreeFactory<Http::HttpMatchingData, ClusterActionContext> factory(
      action_context, context, validation_visitor);

  auto matcher = factory.create(typed_config.cluster_matcher())();
  return std::make_shared<MatcherClusterSpecifierPlugin>(std::move(matcher));
}

REGISTER_FACTORY(MatcherClusterSpecifierPluginFactoryConfig,
                 Envoy::Router::ClusterSpecifierPluginFactoryConfig);

} // namespace Matcher
} // namespace Router
} // namespace Extensions
} // namespace Envoy
