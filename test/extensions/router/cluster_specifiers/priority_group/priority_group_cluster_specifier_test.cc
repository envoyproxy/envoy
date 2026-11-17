#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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

using testing::_;
using testing::NiceMock;

class PriorityGroupClusterSpecifierPluginTest : public testing::Test {
public:
  void setUpTest(const std::string& yaml) {
    PriorityGroupClusterSpecifierConfigProto proto_config{};
    TestUtility::loadFromYaml(yaml, proto_config);

    PriorityGroupClusterSpecifierPluginFactoryConfig factory;
    plugin_ = factory.createClusterSpecifierPlugin(proto_config, server_factory_context_);
  }

  // Set the untyped dynamic metadata that provides the per-request group override.
  void setGroupOverrideMetadata(const std::string& value_yaml) {
    Protobuf::Struct value;
    TestUtility::loadFromYaml(value_yaml, value);
    (*stream_info_.metadata_.mutable_filter_metadata())["envoy.test"] = value;
  }

  // Set the typed dynamic metadata that provides the per-request group override.
  void setTypedGroupOverrideMetadata(const std::string& value_yaml) {
    PriorityGroupsOverrideProto value;
    TestUtility::loadFromYaml(value_yaml, value);
    Protobuf::Any any;
    std::ignore = any.PackFrom(value);
    (*stream_info_.metadata_.mutable_typed_filter_metadata())["envoy.test"] = any;
  }

  // Set the dynamic metadata that overrides the clusters of a single group with the given raw
  // number weights. The YAML helper above could not be used for these cases because the YAML
  // loader converts every float and every integer out of the int32 range to a string value.
  void setClusterOverrideMetadata(const std::string& group_name,
                                  const std::vector<std::pair<std::string, double>>& clusters) {
    Protobuf::Struct value;
    auto& group = *(*value.mutable_fields())["priority_groups"]
                       .mutable_list_value()
                       ->add_values()
                       ->mutable_struct_value();
    (*group.mutable_fields())["name"].set_string_value(group_name);
    auto& cluster_values = *(*group.mutable_fields())["clusters"].mutable_list_value();
    for (const auto& cluster : clusters) {
      auto& cluster_struct = *cluster_values.add_values()->mutable_struct_value();
      (*cluster_struct.mutable_fields())["cluster_name"].set_string_value(cluster.first);
      (*cluster_struct.mutable_fields())["weight"].set_number_value(cluster.second);
    }
    (*stream_info_.metadata_.mutable_filter_metadata())["envoy.test"] = value;
  }

  const std::string config_yaml = R"EOF(
priority_groups:
- name: local
  clusters:
  - cluster_name: local_primary
    weight: 80
  - cluster_name: local_secondary
    weight: 20
- name: remote
  clusters:
  - cluster_name: remote_primary
    weight: 100
  )EOF";

  const std::string config_yaml_with_metadata = R"EOF(
priority_groups:
- name: local
  clusters:
  - cluster_name: local_primary
    weight: 100
- name: remote
  clusters:
  - cluster_name: remote_primary
    weight: 100
override_metadata_namespace: envoy.test
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Http::TestRequestHeaderMapImpl headers_{{":path", "/"}};
  std::shared_ptr<Envoy::Router::ClusterSpecifierPlugin> plugin_;
};

// Direct test of the group entry to cover the cases that the plugin never triggers.
TEST(PriorityGroupEntryTest, SelectClusterFromEntry) {
  {
    PriorityGroupEntry entry("group", {{"cluster_1", 1}, {"cluster_2", 2}});
    EXPECT_EQ("group", entry.name());
    EXPECT_EQ(2, entry.clusters().size());

    // The intervals of the clusters are [0, 1) and [1, 3).
    EXPECT_EQ("cluster_1", entry.selectCluster(0));
    EXPECT_EQ("cluster_2", entry.selectCluster(1));
    EXPECT_EQ("cluster_2", entry.selectCluster(2));
    // The random value wraps around the total weight of the group.
    EXPECT_EQ("cluster_1", entry.selectCluster(3));
  }

  // A group without any weighted cluster is never used for the cluster selection.
  {
    PriorityGroupEntry entry("group", {});
    EXPECT_TRUE(entry.clusters().empty());
    EXPECT_ENVOY_BUG(EXPECT_EQ("", entry.selectCluster(0)),
                     "cluster selection on a priority group without any weighted cluster");
  }
}

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

  // The attempt count exceeds the number of the groups and the request stays on the last group.
  {
    stream_info_.setAttemptCount(3);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("remote_primary", route->routeEntry()->clusterName());

    stream_info_.setAttemptCount(10);
    route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("remote_primary", route->routeEntry()->clusterName());
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

// The configured group order is used if the request has no group override metadata at all.
TEST_F(PriorityGroupClusterSpecifierPluginTest, NoGroupOverrideMetadataInRequest) {
  setUpTest(config_yaml_with_metadata);
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();

  stream_info_.setAttemptCount(1);
  EXPECT_EQ("local_primary",
            plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());

  // The dynamic metadata of another namespace is ignored.
  Protobuf::Struct value;
  TestUtility::loadFromYaml("priority_groups: [{name: remote}]", value);
  (*stream_info_.metadata_.mutable_filter_metadata())["envoy.other"] = value;
  EXPECT_EQ("local_primary",
            plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());

  // The expected namespace exists but the priority_groups field is missing.
  TestUtility::loadFromYaml("other_groups: [{name: remote}]", value);
  (*stream_info_.metadata_.mutable_filter_metadata())["envoy.test"] = value;
  EXPECT_EQ("local_primary",
            plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());
}

// The group overrides in the dynamic metadata override the configured group order.
TEST_F(PriorityGroupClusterSpecifierPluginTest, GroupOverrideMetadata) {
  setUpTest(config_yaml_with_metadata);
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();

  setGroupOverrideMetadata(R"EOF(
priority_groups:
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

  // The attempt count exceeds the size of the metadata list and the request stays on the last
  // group override.
  {
    stream_info_.setAttemptCount(3);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("local_primary", route->routeEntry()->clusterName());
  }

  // Unknown group name in the metadata falls back to the configured group order.
  {
    setGroupOverrideMetadata(R"EOF(
priority_groups:
- name: unknown
  )EOF");
    stream_info_.setAttemptCount(1);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("local_primary", route->routeEntry()->clusterName());
  }
}

// The clusters of a group could also be overridden by the dynamic metadata.
TEST_F(PriorityGroupClusterSpecifierPluginTest, ClusterOverrideMetadata) {
  setUpTest(config_yaml_with_metadata);
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();

  setGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  clusters:
  - cluster_name: remote_override_primary
    weight: 20
  - cluster_name: remote_override_secondary
    weight: 80
- name: local
  )EOF");

  // The initial attempt uses the clusters and the weights of the metadata.
  {
    stream_info_.setAttemptCount(1);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("remote_override_primary", route->routeEntry()->clusterName());
  }

  // Random value 20 falls into the interval of the second cluster of the metadata.
  {
    stream_info_.setAttemptCount(1);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 20);
    EXPECT_EQ("remote_override_secondary", route->routeEntry()->clusterName());
  }

  // The random value wraps around the total weight of the overridden clusters.
  {
    stream_info_.setAttemptCount(1);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 100);
    EXPECT_EQ("remote_override_primary", route->routeEntry()->clusterName());
  }

  // The second group override only overrides the name, so the configured clusters of the group
  // 'local' are used for the first retry.
  {
    stream_info_.setAttemptCount(2);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("local_primary", route->routeEntry()->clusterName());
  }

  // The clusters of a group that is not configured at all could also be overridden.
  {
    setGroupOverrideMetadata(R"EOF(
priority_groups:
- name: unknown
  clusters:
  - cluster_name: unknown_primary
    weight: 100
  )EOF");
    stream_info_.setAttemptCount(1);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("unknown_primary", route->routeEntry()->clusterName());
  }

  // An empty cluster list is treated as a name only override.
  {
    setGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  clusters: []
  )EOF");
    stream_info_.setAttemptCount(1);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("remote_primary", route->routeEntry()->clusterName());
  }

  // The cluster weight is truncated to an integer and the max weight is accepted. The intervals of
  // the clusters below are [0, 2) and [2, 4294967297).
  {
    setClusterOverrideMetadata(
        "remote", {{"remote_override_primary", 2.9}, {"remote_override_secondary", 4294967295.0}});
    stream_info_.setAttemptCount(1);
    EXPECT_EQ("remote_override_primary",
              plugin_->route(mock_route, headers_, stream_info_, 1)->routeEntry()->clusterName());
    EXPECT_EQ("remote_override_secondary",
              plugin_->route(mock_route, headers_, stream_info_, 2)->routeEntry()->clusterName());
  }

  // The overridden clusters are also used for the retries of the request.
  {
    setGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  clusters:
  - cluster_name: remote_override_primary
    weight: 100
  )EOF");
    stream_info_.setAttemptCount(1);
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("remote_override_primary", route->routeEntry()->clusterName());

    // Simulate a retry. The metadata has a single group override, so the request stays on that
    // group override.
    stream_info_.setAttemptCount(2);
    route->routeEntry()->refreshRouteCluster(headers_, stream_info_);
    EXPECT_EQ("remote_override_primary", route->routeEntry()->clusterName());
  }
}

// Malformed group overrides in the dynamic metadata fall back to the configured group order.
TEST_F(PriorityGroupClusterSpecifierPluginTest, MalformedGroupOverrideMetadata) {
  setUpTest(config_yaml_with_metadata);
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();

  // The initial attempt uses the first configured group and the first retry uses the second one.
  const auto expectDefaultGroupOrder = [&]() {
    stream_info_.setAttemptCount(1);
    EXPECT_EQ("local_primary",
              plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());
    stream_info_.setAttemptCount(2);
    EXPECT_EQ("remote_primary",
              plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());
  };

  // The metadata value is not a list.
  setGroupOverrideMetadata(R"EOF(
priority_groups:
  name: remote
  )EOF");
  expectDefaultGroupOrder();

  // The metadata value is an empty list.
  setGroupOverrideMetadata(R"EOF(
priority_groups: []
  )EOF");
  expectDefaultGroupOrder();

  // The element of the list is not a struct.
  setGroupOverrideMetadata(R"EOF(
priority_groups:
- remote
- local
  )EOF");
  expectDefaultGroupOrder();

  // The group name is missing, is not a string, or is empty.
  setGroupOverrideMetadata(R"EOF(
priority_groups:
- clusters:
  - cluster_name: remote_primary
    weight: 100
- name: 1
- name: ""
  )EOF");
  expectDefaultGroupOrder();
  // The third attempt falls back to the configured groups, which stay on the last group.
  stream_info_.setAttemptCount(3);
  EXPECT_EQ("remote_primary",
            plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());

  // The clusters are not a list.
  setGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  clusters:
    cluster_name: remote_primary
  )EOF");
  expectDefaultGroupOrder();

  // The element of the clusters is not a struct.
  setGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  clusters:
  - remote_primary
  )EOF");
  expectDefaultGroupOrder();

  // The cluster name is missing, is not a string, or is empty.
  setGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  clusters:
  - weight: 100
- name: remote
  clusters:
  - cluster_name: ""
    weight: 100
- name: remote
  clusters:
  - cluster_name: 1
    weight: 100
  )EOF");
  expectDefaultGroupOrder();
  // The third attempt falls back to the configured groups, which stay on the last group.
  stream_info_.setAttemptCount(3);
  EXPECT_EQ("remote_primary",
            plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());

  // The cluster weight is missing, is not a number, or is not a positive integer.
  setGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  clusters:
  - cluster_name: remote_primary
- name: remote
  clusters:
  - cluster_name: remote_primary
    weight: "100"
  )EOF");
  expectDefaultGroupOrder();

  setGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  clusters:
  - cluster_name: remote_primary
    weight: 0
- name: remote
  clusters:
  - cluster_name: remote_primary
    weight: -1
  )EOF");
  expectDefaultGroupOrder();

  // The cluster weight is less than 1, is greater than the max value of uint32, or is not a
  // number at all.
  for (const double weight : {0.5, 4294967296.0, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()}) {
    setClusterOverrideMetadata("remote", {{"remote_override_primary", weight}});
    stream_info_.setAttemptCount(1);
    EXPECT_EQ("local_primary",
              plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());
  }

  // One malformed cluster invalidates the whole group override to avoid an unexpected traffic
  // distribution.
  setClusterOverrideMetadata(
      "remote", {{"remote_override_primary", 100.0}, {"remote_override_secondary", 0.0}});
  expectDefaultGroupOrder();
}

// Only the first group of the duplicate names could be referenced by the group override metadata,
// but the positional selection is not affected.
TEST_F(PriorityGroupClusterSpecifierPluginTest, DuplicateGroupName) {
  setUpTest(R"EOF(
priority_groups:
- name: local
  clusters:
  - cluster_name: local_primary
    weight: 100
- name: local
  clusters:
  - cluster_name: local_secondary
    weight: 100
override_metadata_namespace: envoy.test
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
priority_groups:
- name: local
  )EOF");
    auto route = plugin_->route(mock_route, headers_, stream_info_, 0);
    EXPECT_EQ("local_primary", route->routeEntry()->clusterName());
  }
}

// The random value of the weighted cluster selection could be read from a request header.
TEST_F(PriorityGroupClusterSpecifierPluginTest, RandomValueFromHeader) {
  setUpTest(config_yaml + R"EOF(
header_name: x-random-value
  )EOF");
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  stream_info_.setAttemptCount(1);

  // The header is not present at all and the internally generated random value is used.
  EXPECT_EQ("local_secondary",
            plugin_->route(mock_route, headers_, stream_info_, 85)->routeEntry()->clusterName());

  // The random value of the header is used and the given random value is ignored.
  headers_.setCopy(Http::LowerCaseString("x-random-value"), "10");
  EXPECT_EQ("local_primary",
            plugin_->route(mock_route, headers_, stream_info_, 85)->routeEntry()->clusterName());

  headers_.setCopy(Http::LowerCaseString("x-random-value"), "85");
  EXPECT_EQ("local_secondary",
            plugin_->route(mock_route, headers_, stream_info_, 10)->routeEntry()->clusterName());

  // The max value of uint64 is accepted. It wraps around the total weight of the group to 15 and
  // falls into the interval of the first cluster.
  headers_.setCopy(Http::LowerCaseString("x-random-value"),
                   std::to_string(std::numeric_limits<uint64_t>::max()));
  EXPECT_EQ("local_primary",
            plugin_->route(mock_route, headers_, stream_info_, 85)->routeEntry()->clusterName());

  // The header value is not a valid number and the internally generated random value is used.
  headers_.setCopy(Http::LowerCaseString("x-random-value"), "invalid");
  EXPECT_EQ("local_secondary",
            plugin_->route(mock_route, headers_, stream_info_, 85)->routeEntry()->clusterName());

  // The header is multi-valued and the internally generated random value is used.
  headers_.setCopy(Http::LowerCaseString("x-random-value"), "10");
  headers_.addCopy(Http::LowerCaseString("x-random-value"), "20");
  EXPECT_EQ("local_secondary",
            plugin_->route(mock_route, headers_, stream_info_, 85)->routeEntry()->clusterName());

  // The random value of the header is also used for the retries of the request.
  headers_.setCopy(Http::LowerCaseString("x-random-value"), "10");
  auto route = plugin_->route(mock_route, headers_, stream_info_, 85);
  EXPECT_EQ("local_primary", route->routeEntry()->clusterName());
  stream_info_.setAttemptCount(2);
  route->routeEntry()->refreshRouteCluster(headers_, stream_info_);
  EXPECT_EQ("remote_primary", route->routeEntry()->clusterName());
}

// The random value of the weighted cluster selection could be generated by the hash policies.
TEST_F(PriorityGroupClusterSpecifierPluginTest, RandomValueFromHashPolicy) {
  setUpTest(config_yaml + R"EOF(
use_hash_policy: true
  )EOF");
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  stream_info_.setAttemptCount(1);

  // The route has no hash policy at all and the internally generated random value is used.
  ON_CALL(*mock_route, hashPolicy()).WillByDefault(testing::Return(nullptr));
  EXPECT_EQ("local_secondary",
            plugin_->route(mock_route, headers_, stream_info_, 85)->routeEntry()->clusterName());

  // The hash of the hash policies is used and the given random value is ignored.
  NiceMock<Envoy::Router::MockHashPolicy> hash_policy;
  ON_CALL(*mock_route, hashPolicy()).WillByDefault(testing::Return(&hash_policy));
  EXPECT_CALL(hash_policy, generateHash(_, _, _))
      .WillOnce(testing::Return(std::optional<uint64_t>(10)));
  EXPECT_EQ("local_primary",
            plugin_->route(mock_route, headers_, stream_info_, 85)->routeEntry()->clusterName());

  EXPECT_CALL(hash_policy, generateHash(_, _, _))
      .WillOnce(testing::Return(std::optional<uint64_t>(85)));
  EXPECT_EQ("local_secondary",
            plugin_->route(mock_route, headers_, stream_info_, 10)->routeEntry()->clusterName());

  // The hash policies generate no hash at all and the internally generated random value is used.
  EXPECT_CALL(hash_policy, generateHash(_, _, _))
      .WillOnce(testing::Return(std::optional<uint64_t>{}));
  EXPECT_EQ("local_secondary",
            plugin_->route(mock_route, headers_, stream_info_, 85)->routeEntry()->clusterName());

  // The hash is also used for the retries of the request and is only computed once.
  EXPECT_CALL(hash_policy, generateHash(_, _, _))
      .WillOnce(testing::Return(std::optional<uint64_t>(10)));
  auto route = plugin_->route(mock_route, headers_, stream_info_, 85);
  EXPECT_EQ("local_primary", route->routeEntry()->clusterName());
  stream_info_.setAttemptCount(2);
  route->routeEntry()->refreshRouteCluster(headers_, stream_info_);
  EXPECT_EQ("remote_primary", route->routeEntry()->clusterName());
}

// The hash policies are only used if the use_hash_policy is explicitly enabled.
TEST_F(PriorityGroupClusterSpecifierPluginTest, HashPolicyDisabled) {
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  NiceMock<Envoy::Router::MockHashPolicy> hash_policy;
  ON_CALL(*mock_route, hashPolicy()).WillByDefault(testing::Return(&hash_policy));
  stream_info_.setAttemptCount(1);

  // The random value specifier is not configured at all.
  setUpTest(config_yaml);
  EXPECT_CALL(hash_policy, generateHash(_, _, _)).Times(0);
  EXPECT_EQ("local_secondary",
            plugin_->route(mock_route, headers_, stream_info_, 85)->routeEntry()->clusterName());

  // The use_hash_policy is explicitly set to false.
  setUpTest(config_yaml + R"EOF(
use_hash_policy: false
  )EOF");
  EXPECT_CALL(hash_policy, generateHash(_, _, _)).Times(0);
  EXPECT_EQ("local_secondary",
            plugin_->route(mock_route, headers_, stream_info_, 85)->routeEntry()->clusterName());
}

TEST_F(PriorityGroupClusterSpecifierPluginTest, ValidateClusters) {
  setUpTest(config_yaml);
  NiceMock<Upstream::MockClusterManager> cm;

  // All the clusters of all the groups are validated.
  EXPECT_CALL(cm, hasCluster("local_primary")).WillOnce(testing::Return(true));
  EXPECT_CALL(cm, hasCluster("local_secondary")).WillOnce(testing::Return(true));
  EXPECT_CALL(cm, hasCluster("remote_primary")).WillOnce(testing::Return(true));
  EXPECT_TRUE(plugin_->validateClusters(cm).ok());

  EXPECT_CALL(cm, hasCluster("local_primary")).WillOnce(testing::Return(true));
  EXPECT_CALL(cm, hasCluster("local_secondary")).WillOnce(testing::Return(false));
  EXPECT_EQ(plugin_->validateClusters(cm).message(),
            "route: unknown cluster 'local_secondary' in priority group 'local'");
}

TEST(SenselessTestForCoverage, SenselessTestForCoverage) {
  PriorityGroupClusterSpecifierPluginFactoryConfig factory;
  EXPECT_EQ("envoy.router.cluster_specifier_plugin.priority_group", factory.name());
  EXPECT_NE(nullptr, factory.createEmptyConfigProto());
}

// The typed dynamic metadata carries the group overrides as a PriorityGroupsOverride message.
TEST_F(PriorityGroupClusterSpecifierPluginTest, TypedGroupOverrideMetadata) {
  setUpTest(config_yaml_with_metadata);
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();

  setTypedGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  clusters:
  - cluster_name: remote_override_primary
    weight: 20
  - cluster_name: remote_override_secondary
    weight: 80
- name: local
  )EOF");

  // The initial attempt uses the clusters and the weights of the first group override.
  {
    stream_info_.setAttemptCount(1);
    EXPECT_EQ("remote_override_primary",
              plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());
    EXPECT_EQ("remote_override_secondary",
              plugin_->route(mock_route, headers_, stream_info_, 20)->routeEntry()->clusterName());
  }

  // The second group override only overrides the name, so the configured clusters of the group
  // 'local' are used for the first retry.
  {
    stream_info_.setAttemptCount(2);
    EXPECT_EQ("local_primary",
              plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());
  }

  // The attempt count exceeds the size of the metadata list and the request stays on the last
  // group override.
  {
    stream_info_.setAttemptCount(3);
    EXPECT_EQ("local_primary",
              plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());
  }

  // An unknown group name without any cluster falls back to the configured group order.
  {
    setTypedGroupOverrideMetadata(R"EOF(
priority_groups:
- name: unknown
  )EOF");
    stream_info_.setAttemptCount(1);
    EXPECT_EQ("local_primary",
              plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());
  }
}

// The typed dynamic metadata is preferred over the untyped one of the same namespace.
TEST_F(PriorityGroupClusterSpecifierPluginTest, TypedGroupOverrideMetadataTakesPrecedence) {
  setUpTest(config_yaml_with_metadata);
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  stream_info_.setAttemptCount(1);

  setGroupOverrideMetadata(R"EOF(
priority_groups:
- name: local
  )EOF");
  setTypedGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  )EOF");
  EXPECT_EQ("remote_primary",
            plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());

  // A typed metadata entry that cannot be used does not fall back to the untyped one either.
  Protobuf::Any any;
  std::ignore = any.PackFrom(Protobuf::Struct());
  (*stream_info_.metadata_.mutable_typed_filter_metadata())["envoy.test"] = any;
  EXPECT_EQ("local_primary",
            plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());
}

// Malformed group overrides in the typed dynamic metadata fall back to the configured group order.
TEST_F(PriorityGroupClusterSpecifierPluginTest, MalformedTypedGroupOverrideMetadata) {
  setUpTest(config_yaml_with_metadata);
  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  stream_info_.setAttemptCount(1);

  // The typed metadata holds no group at all.
  setTypedGroupOverrideMetadata("{}");
  EXPECT_EQ("local_primary",
            plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());

  // The group name is empty.
  setTypedGroupOverrideMetadata(R"EOF(
priority_groups:
- clusters:
  - cluster_name: remote_override_primary
    weight: 100
  )EOF");
  EXPECT_EQ("local_primary",
            plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());

  // The cluster name is empty.
  setTypedGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  clusters:
  - weight: 100
  )EOF");
  EXPECT_EQ("local_primary",
            plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());

  // The cluster weight is missing or is zero. One malformed cluster invalidates the whole group
  // override to avoid an unexpected traffic distribution.
  setTypedGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  clusters:
  - cluster_name: remote_override_primary
  )EOF");
  EXPECT_EQ("local_primary",
            plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());

  setTypedGroupOverrideMetadata(R"EOF(
priority_groups:
- name: remote
  clusters:
  - cluster_name: remote_override_primary
    weight: 100
  - cluster_name: remote_override_secondary
    weight: 0
  )EOF");
  EXPECT_EQ("local_primary",
            plugin_->route(mock_route, headers_, stream_info_, 0)->routeEntry()->clusterName());
}

} // namespace
} // namespace PriorityGroup
} // namespace Router
} // namespace Extensions
} // namespace Envoy
