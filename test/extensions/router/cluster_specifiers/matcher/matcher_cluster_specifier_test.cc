#include "source/extensions/router/cluster_specifiers/matcher/config.h"
#include "source/extensions/router/cluster_specifiers/matcher/matcher_cluster_specifier.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Matcher {
namespace {

using testing::NiceMock;

class MatcherClusterSpecifierPluginTest : public testing::Test {
public:
  void setUpTest(const std::string& yaml) {
    MatcherClusterSpecifierConfigProto proto_config{};
    TestUtility::loadFromYaml(yaml, proto_config);

    MatcherClusterSpecifierPluginFactoryConfig factory;

    plugin_ = factory.createClusterSpecifierPlugin(proto_config, server_factory_context_);
  }

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

  const std::string normal_config_yaml_without_default_cluster = R"EOF(
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
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::shared_ptr<Envoy::Router::ClusterSpecifierPlugin> plugin_;
};

TEST_F(MatcherClusterSpecifierPluginTest, NormalConfigTest) {
  setUpTest(normal_config_yaml);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "staging"}};
    auto route = plugin_->route(mock_route, headers, stream_info_);
    EXPECT_EQ("staging-cluster", route->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "prod"}};
    auto route = plugin_->route(mock_route, headers, stream_info_);
    EXPECT_EQ("prod-cluster", route->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "not-exist"}};
    auto route = plugin_->route(mock_route, headers, stream_info_);
    EXPECT_EQ("default-cluster", route->routeEntry()->clusterName());
  }
}

TEST_F(MatcherClusterSpecifierPluginTest, NormalConfigWithoutDefaultCluster) {
  setUpTest(normal_config_yaml_without_default_cluster);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "staging"}};
    auto route = plugin_->route(mock_route, headers, stream_info_);
    EXPECT_EQ("staging-cluster", route->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"env", "not-exist"}};
    auto route = plugin_->route(mock_route, headers, stream_info_);
    EXPECT_EQ("", route->routeEntry()->clusterName());
  }
}

TEST(SenselessTestForCoverage, SenselessTestForCoverage) {
  ClusterActionFactory factory;
  EXPECT_EQ("cluster", factory.name());
}

} // namespace
} // namespace Matcher
} // namespace Router
} // namespace Extensions
} // namespace Envoy
