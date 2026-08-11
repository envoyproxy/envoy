#include "source/extensions/router/cluster_specifiers/priority_group/config.h"
#include "source/extensions/router/cluster_specifiers/priority_group/priority_group_cluster_specifier.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace PriorityGroup {
namespace {

using testing::NiceMock;

class PriorityGroupClusterSpecifierPluginTest : public testing::Test {
public:
  void setUpTest(const std::string& yaml) {
    PriorityGroupClusterSpecifierConfigProto proto_config{};
    TestUtility::loadFromYaml(yaml, proto_config);

    PriorityGroupClusterSpecifierPluginFactoryConfig factory;
    plugin_ = factory.createClusterSpecifierPlugin(proto_config, server_factory_context_);
  }

  // Set the dynamic metadata that provides the per-request group override.
  void setGroupOverrideMetadata(const std::string& value_yaml) {
    Protobuf::Struct value;
    TestUtility::loadFromYaml(value_yaml, value);
    (*stream_info_.metadata_.mutable_filter_metadata())["envoy.test"] = value;
  }

  const std::string config_yaml = R"EOF(
priority_groups:
- name: local
  clusters:
  - name: local_primary
    weight: 80
  - name: local_secondary
    weight: 20
- name: remote
  clusters:
  - name: remote_primary
    weight: 100
  )EOF";

  const std::string config_yaml_with_metadata = R"EOF(
priority_groups:
- name: local
  clusters:
  - name: local_primary
    weight: 100
- name: remote
  clusters:
  - name: remote_primary
    weight: 100
group_override_metadata:
  key: envoy.test
  path:
  - key: groups
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Http::TestRequestHeaderMapImpl headers_{{":path", "/"}};
  std::shared_ptr<Envoy::Router::ClusterSpecifierPlugin> plugin_;
};

// The group is selected by the attempt count and the cluster is selected by the weight.
TEST_F(PriorityGroupClusterSpecifierPluginTest, SelectGroupByAttempt) {
  setUpTest(config_yaml);
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();

  // The attempt count is not set yet when the route is selected. The first group is used.
  {
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("local_primary", route->routeEntry()->clusterName());
  }

  // Random value 85 falls into the interval of the second cluster of the first group.
  {
    auto route = plugin_->route(mock_route, headers_, stream_info_, 85);
    EXPECT_EQ("local_secondary", route->routeEntry()->clusterName());
  }

  // The initial attempt uses the first group.
  {
    stream_info_.setAttemptCount(1);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("local_primary", route->routeEntry()->clusterName());
  }

  // The first retry uses the second group.
  {
    stream_info_.setAttemptCount(2);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("remote_primary", route->routeEntry()->clusterName());
  }

  // The attempt count exceeds the number of the groups and the selection wraps around.
  {
    stream_info_.setAttemptCount(3);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("local_primary", route->routeEntry()->clusterName());
  }
}

// The target cluster is refreshed based on the attempt count of the current attempt.
TEST_F(PriorityGroupClusterSpecifierPluginTest, RefreshRouteCluster) {
  setUpTest(config_yaml);
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();

  stream_info_.setAttemptCount(1);
  auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
  EXPECT_EQ("local_primary", route->routeEntry()->clusterName());

  // Simulate a retry.
  stream_info_.setAttemptCount(2);
  route->routeEntry()->refreshRouteCluster(headers_, stream_info_);
  EXPECT_EQ("remote_primary", route->routeEntry()->clusterName());
}

// The group overrides in the dynamic metadata override the configured group order.
TEST_F(PriorityGroupClusterSpecifierPluginTest, GroupOverrideMetadata) {
  setUpTest(config_yaml_with_metadata);
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();

  setGroupOverrideMetadata(R"EOF(
groups:
- name: remote
- name: local
  )EOF");

  // The initial attempt uses the first group override of the metadata.
  {
    stream_info_.setAttemptCount(1);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("remote_primary", route->routeEntry()->clusterName());
  }

  // The first retry uses the second group override of the metadata.
  {
    stream_info_.setAttemptCount(2);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("local_primary", route->routeEntry()->clusterName());
  }

  // The attempt count exceeds the size of the metadata list and the selection wraps around.
  {
    stream_info_.setAttemptCount(3);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("remote_primary", route->routeEntry()->clusterName());
  }

  // Unknown group name in the metadata falls back to the configured group order.
  {
    setGroupOverrideMetadata(R"EOF(
groups:
- name: unknown
  )EOF");
    stream_info_.setAttemptCount(1);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("local_primary", route->routeEntry()->clusterName());
  }
}

// Only the first group of the duplicate names could be referenced by the group override metadata,
// but the positional selection is not affected.
TEST_F(PriorityGroupClusterSpecifierPluginTest, DuplicateGroupName) {
  setUpTest(R"EOF(
priority_groups:
- name: local
  clusters:
  - name: local_primary
    weight: 100
- name: local
  clusters:
  - name: local_secondary
    weight: 100
group_override_metadata:
  key: envoy.test
  path:
  - key: groups
  )EOF");
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();

  stream_info_.setAttemptCount(2);
  {
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("local_secondary", route->routeEntry()->clusterName());
  }

  // The name always resolves to the first group of the duplicate names.
  {
    setGroupOverrideMetadata(R"EOF(
groups:
- name: local
  )EOF");
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("local_primary", route->routeEntry()->clusterName());
  }
}

TEST_F(PriorityGroupClusterSpecifierPluginTest, ValidateClusters) {
  setUpTest(config_yaml);
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_CALL(cm, hasCluster("local_primary")).WillOnce(testing::Return(true));
  EXPECT_CALL(cm, hasCluster("local_secondary")).WillOnce(testing::Return(false));
  EXPECT_EQ(plugin_->validateClusters(cm).message(),
            "route: unknown cluster 'local_secondary' in priority group 'local'");
}

TEST(SenselessTestForCoverage, SenselessTestForCoverage) {
  PriorityGroupClusterSpecifierPluginFactoryConfig factory;
  EXPECT_EQ("envoy.router.cluster_specifier_plugin.priority_group", factory.name());
}

} // namespace
} // namespace PriorityGroup
} // namespace Router
} // namespace Extensions
} // namespace Envoy
