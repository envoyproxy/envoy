#include "source/extensions/router/cluster_specifiers/matcher/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Matcher {
namespace {

TEST(MatcherClusterSpecifierPluginConfigTest, EmptyConfig) {
  MatcherClusterSpecifierPluginFactoryConfig factory;

  ProtobufTypes::MessagePtr empty_config = factory.createEmptyConfigProto();
  EXPECT_NE(nullptr, empty_config);
}

TEST(MatcherClusterSpecifierPluginConfigTest, NormalConfig) {
  const std::string normal_config_yaml = R"EOF(
cluster_matcher:
  matcher_list:
    matchers:
    - predicate:
        single_predicate:
          input:
            name: "header"
            typed_config:
              '@type': type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
              header_name: env
          value_match:
            exact: staging
      on_match:
        action:
          name: "staging-cluster"
          typed_config:
            '@type': type.googleapis.com/envoy.extensions.router.cluster_specifiers.matcher.v3.ClusterAction
            cluster: "staging-cluster"
    - predicate:
        single_predicate:
          input:
            name: "header"
            typed_config:
              '@type': type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
              header_name: env
          value_match:
            exact: prod
      on_match:
        action:
          name: "prod-cluster"
          typed_config:
            '@type': type.googleapis.com/envoy.extensions.router.cluster_specifiers.matcher.v3.ClusterAction
            cluster: "prod-cluster"
  # Catch-all with a default cluster.
  on_no_match:
    action:
      name: "default-cluster"
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.router.cluster_specifiers.matcher.v3.ClusterAction
        cluster: "default-cluster"
  )EOF";

  MatcherClusterSpecifierConfigProto proto_config{};
  TestUtility::loadFromYaml(normal_config_yaml, proto_config);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  MatcherClusterSpecifierPluginFactoryConfig factory;
  Envoy::Router::ClusterSpecifierPluginSharedPtr plugin =
      factory.createClusterSpecifierPlugin(proto_config, context);
  EXPECT_NE(nullptr, plugin);
}

} // namespace
} // namespace Matcher
} // namespace Router
} // namespace Extensions
} // namespace Envoy
