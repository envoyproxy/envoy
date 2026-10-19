#include "source/extensions/router/cluster_specifiers/header_weighted/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace HeaderWeighted {
namespace {

TEST(HeaderWeightedClusterSpecifierPluginConfigTest, FactoryName) {
  HeaderWeightedClusterSpecifierPluginFactoryConfig factory;
  EXPECT_EQ("envoy.router.cluster_specifier_plugin.header_weighted",
            factory.name());
}

TEST(HeaderWeightedClusterSpecifierPluginConfigTest, EmptyConfig) {
  HeaderWeightedClusterSpecifierPluginFactoryConfig factory;

  ProtobufTypes::MessagePtr empty_config = factory.createEmptyConfigProto();
  EXPECT_NE(nullptr, empty_config);
}

TEST(HeaderWeightedClusterSpecifierPluginConfigTest, CreatePluginFromConfig) {
  const std::string config_yaml = R"EOF(
header: x-target-cluster
clusters:
- name: cluster-a
  weight: 30
- name: cluster-b
  weight: 70
default_cluster: cluster-c
  )EOF";

  HeaderWeightedConfigProto proto_config{};
  TestUtility::loadFromYaml(config_yaml, proto_config);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  HeaderWeightedClusterSpecifierPluginFactoryConfig factory;
  Envoy::Router::ClusterSpecifierPluginSharedPtr plugin =
      factory.createClusterSpecifierPlugin(proto_config, context);
  EXPECT_NE(nullptr, plugin);
}

} // namespace
} // namespace HeaderWeighted
} // namespace Router
} // namespace Extensions
} // namespace Envoy
