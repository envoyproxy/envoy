#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/composite/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {

namespace {
const std::string primary_name("primary");
} // namespace

// Test basic proto parsing and cluster creation.
TEST(CompositeClusterTest, BasicProtoValidation) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - primary_cluster
    - secondary_cluster
)EOF";

  auto cluster_config = Upstream::parseClusterFromV3Yaml(yaml);
  EXPECT_EQ("composite_cluster", cluster_config.name());
  EXPECT_EQ("envoy.clusters.composite", cluster_config.cluster_type().name());

  envoy::extensions::clusters::composite::v3::ClusterConfig config;
  THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
      cluster_config.cluster_type().typed_config(), ProtobufMessage::getStrictValidationVisitor(),
      config));

  EXPECT_EQ(2, config.clusters_size());
  EXPECT_EQ("primary_cluster", config.clusters(0));
  EXPECT_EQ("secondary_cluster", config.clusters(1));
  EXPECT_EQ(envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL,
            config.overflow_option());
}

// Test overflow option configuration.
TEST(CompositeClusterTest, OverflowOptionConfiguration) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - primary_cluster
    - secondary_cluster
    - tertiary_cluster
    overflow_option: USE_LAST_CLUSTER
)EOF";

  auto cluster_config = Upstream::parseClusterFromV3Yaml(yaml);
  envoy::extensions::clusters::composite::v3::ClusterConfig config;
  THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
      cluster_config.cluster_type().typed_config(), ProtobufMessage::getStrictValidationVisitor(),
      config));

  EXPECT_EQ(3, config.clusters_size());
  EXPECT_EQ(envoy::extensions::clusters::composite::v3::ClusterConfig::USE_LAST_CLUSTER,
            config.overflow_option());
}

// Test round robin overflow configuration.
TEST(CompositeClusterTest, RoundRobinOverflowConfiguration) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - primary_cluster
    - secondary_cluster
    overflow_option: ROUND_ROBIN
)EOF";

  auto cluster_config = Upstream::parseClusterFromV3Yaml(yaml);
  envoy::extensions::clusters::composite::v3::ClusterConfig config;
  THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
      cluster_config.cluster_type().typed_config(), ProtobufMessage::getStrictValidationVisitor(),
      config));

  EXPECT_EQ(envoy::extensions::clusters::composite::v3::ClusterConfig::ROUND_ROBIN,
            config.overflow_option());
}

// Test factory registration.
TEST(CompositeClusterTest, FactoryRegistration) {
  ClusterFactory factory;
  EXPECT_EQ("envoy.clusters.composite", factory.name());
  EXPECT_EQ("envoy.clusters", factory.category());
}

// Test basic load balancer functionality - retry attempt counting.
TEST(CompositeClusterTest, LoadBalancerRetryAttemptCounting) {
  using envoy::extensions::clusters::composite::v3::ClusterConfig;

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto parent_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  auto cluster_set = std::make_shared<ClusterSet>();
  cluster_set->push_back("primary_cluster");
  cluster_set->push_back("secondary_cluster");

  CompositeClusterLoadBalancer lb(parent_info, cluster_manager, cluster_set, ClusterConfig::FAIL);

  // Test getRetryAttemptCount with null context
  EXPECT_EQ(1, lb.getRetryAttemptCount(nullptr));

  // Test mapRetryAttemptToClusterIndex with FAIL behavior
  EXPECT_FALSE(lb.mapRetryAttemptToClusterIndex(0).has_value());
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(1).value());
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(2).value());
  EXPECT_FALSE(lb.mapRetryAttemptToClusterIndex(3).has_value());
}

// Test USE_LAST_CLUSTER overflow behavior.
TEST(CompositeClusterTest, UseLastClusterOverflow) {
  using envoy::extensions::clusters::composite::v3::ClusterConfig;

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto parent_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  auto cluster_set = std::make_shared<ClusterSet>();
  cluster_set->push_back("primary_cluster");
  cluster_set->push_back("secondary_cluster");

  CompositeClusterLoadBalancer lb(parent_info, cluster_manager, cluster_set,
                                  ClusterConfig::USE_LAST_CLUSTER);

  // Test overflow behavior
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(1).value());
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(2).value());
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(3).value());  // Use last cluster
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(10).value()); // Still use last cluster
}

// Test ROUND_ROBIN overflow behavior.
TEST(CompositeClusterTest, RoundRobinOverflow) {
  using envoy::extensions::clusters::composite::v3::ClusterConfig;

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto parent_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  auto cluster_set = std::make_shared<ClusterSet>();
  cluster_set->push_back("primary_cluster");
  cluster_set->push_back("secondary_cluster");

  CompositeClusterLoadBalancer lb(parent_info, cluster_manager, cluster_set,
                                  ClusterConfig::ROUND_ROBIN);

  // Test round-robin behavior
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(1).value());
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(2).value());
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(3).value()); // Round-robin back to first
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(4).value()); // Round-robin to second
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(5).value()); // Round-robin back to first
}

// Test chooseHost with cluster not found.
TEST(CompositeClusterTest, ChooseHostClusterNotFound) {
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto parent_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  auto cluster_set = std::make_shared<ClusterSet>();
  cluster_set->push_back("nonexistent_cluster");

  CompositeClusterLoadBalancer lb(parent_info, cluster_manager, cluster_set,
                                  envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL);

  EXPECT_CALL(cluster_manager, getThreadLocalCluster("nonexistent_cluster"))
      .WillRepeatedly(Return(nullptr));

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(1));

  auto result = lb.chooseHost(&context);
  EXPECT_EQ(nullptr, result.host);
}

// Test chooseHost with successful cluster selection.
TEST(CompositeClusterTest, ChooseHostSuccess) {
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto parent_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  auto cluster_set = std::make_shared<ClusterSet>();
  cluster_set->push_back("primary_cluster");

  CompositeClusterLoadBalancer lb(parent_info, cluster_manager, cluster_set,
                                  envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL);

  NiceMock<Upstream::MockThreadLocalCluster> primary_cluster;
  NiceMock<Upstream::MockLoadBalancer> primary_lb;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();

  EXPECT_CALL(cluster_manager, getThreadLocalCluster("primary_cluster"))
      .WillRepeatedly(Return(&primary_cluster));
  EXPECT_CALL(primary_cluster, loadBalancer()).WillRepeatedly(ReturnRef(primary_lb));
  EXPECT_CALL(primary_lb, chooseHost(testing::_))
      .WillOnce(Return(Upstream::HostSelectionResponse{mock_host}));

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(1));

  auto result = lb.chooseHost(&context);
  EXPECT_EQ(mock_host, result.host);
}

// Test peekAnotherHost functionality.
TEST(CompositeClusterTest, PeekAnotherHost) {
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto parent_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  auto cluster_set = std::make_shared<ClusterSet>();
  cluster_set->push_back("primary_cluster");

  CompositeClusterLoadBalancer lb(parent_info, cluster_manager, cluster_set,
                                  envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL);

  NiceMock<Upstream::MockThreadLocalCluster> primary_cluster;
  NiceMock<Upstream::MockLoadBalancer> primary_lb;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();

  EXPECT_CALL(cluster_manager, getThreadLocalCluster("primary_cluster"))
      .WillRepeatedly(Return(&primary_cluster));
  EXPECT_CALL(primary_cluster, loadBalancer()).WillRepeatedly(ReturnRef(primary_lb));
  EXPECT_CALL(primary_lb, peekAnotherHost(testing::_)).WillOnce(Return(mock_host));

  NiceMock<Upstream::MockLoadBalancerContext> context;
  auto result = lb.peekAnotherHost(&context);
  EXPECT_EQ(mock_host, result);
}

// Test peekAnotherHost when cluster not found.
TEST(CompositeClusterTest, PeekAnotherHostClusterNotFound) {
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto parent_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  auto cluster_set = std::make_shared<ClusterSet>();
  cluster_set->push_back("missing_cluster");

  CompositeClusterLoadBalancer lb(parent_info, cluster_manager, cluster_set,
                                  envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL);

  EXPECT_CALL(cluster_manager, getThreadLocalCluster("missing_cluster"))
      .WillRepeatedly(Return(nullptr));

  NiceMock<Upstream::MockLoadBalancerContext> context;
  auto result = lb.peekAnotherHost(&context);
  EXPECT_EQ(nullptr, result);
}

// Test selectExistingConnection functionality.
TEST(CompositeClusterTest, SelectExistingConnection) {
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto parent_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  auto cluster_set = std::make_shared<ClusterSet>();
  cluster_set->push_back("primary_cluster");

  CompositeClusterLoadBalancer lb(parent_info, cluster_manager, cluster_set,
                                  envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL);

  NiceMock<Upstream::MockThreadLocalCluster> primary_cluster;
  NiceMock<Upstream::MockLoadBalancer> primary_lb;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();

  EXPECT_CALL(cluster_manager, getThreadLocalCluster("primary_cluster"))
      .WillRepeatedly(Return(&primary_cluster));
  EXPECT_CALL(primary_cluster, loadBalancer()).WillRepeatedly(ReturnRef(primary_lb));

  EXPECT_CALL(primary_lb, selectExistingConnection(testing::_, testing::_, testing::_))
      .WillOnce(Return(absl::nullopt));

  NiceMock<Upstream::MockLoadBalancerContext> context;
  std::vector<uint8_t> hash_key;
  auto result = lb.selectExistingConnection(&context, *mock_host, hash_key);
  EXPECT_FALSE(result.has_value());
}

// Test CompositeLoadBalancerContext with null base context.
TEST(CompositeClusterTest, LoadBalancerContextNullBase) {
  CompositeLoadBalancerContext context(nullptr);

  // Test all methods with null base context return appropriate defaults.
  EXPECT_EQ(absl::nullopt, context.computeHashKey());
  EXPECT_EQ(nullptr, context.downstreamConnection());
  EXPECT_EQ(nullptr, context.metadataMatchCriteria());
  EXPECT_EQ(nullptr, context.downstreamHeaders());
  EXPECT_EQ(nullptr, context.requestStreamInfo());
  EXPECT_EQ(0, context.hostSelectionRetryCount());
  EXPECT_EQ(nullptr, context.upstreamSocketOptions());
  EXPECT_EQ(nullptr, context.upstreamTransportSocketOptions());
  EXPECT_EQ(absl::nullopt, context.overrideHostToSelect());

  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  EXPECT_FALSE(context.shouldSelectAnotherHost(*mock_host));

  // Test methods that accept callbacks with null base context (should not crash).
  std::function<void(Http::ResponseHeaderMap&)> headers_modifier = [](Http::ResponseHeaderMap&) {};
  context.setHeadersModifier(std::move(headers_modifier));

  context.onAsyncHostSelection(mock_host, "test_details");
}

// Test CompositeLoadBalancerContext with non-null base context.
TEST(CompositeClusterTest, LoadBalancerContextWithBase) {
  NiceMock<Upstream::MockLoadBalancerContext> mock_base_context;

  // Set up expectations for key methods.
  EXPECT_CALL(mock_base_context, computeHashKey()).WillOnce(Return(12345));
  EXPECT_CALL(mock_base_context, hostSelectionRetryCount()).WillOnce(Return(3));
  EXPECT_CALL(mock_base_context, overrideHostToSelect())
      .WillOnce(Return(std::make_pair("test_host", true)));

  CompositeLoadBalancerContext context(&mock_base_context);

  // Test that methods delegate to base context.
  EXPECT_EQ(12345, context.computeHashKey().value());
  EXPECT_EQ(3, context.hostSelectionRetryCount());
  auto override_host = context.overrideHostToSelect();
  EXPECT_TRUE(override_host.has_value());
  EXPECT_EQ("test_host", override_host->first);
  EXPECT_TRUE(override_host->second);

  // Test callback methods delegate to base context.
  std::function<void(Http::ResponseHeaderMap&)> headers_modifier = [](Http::ResponseHeaderMap&) {};
  EXPECT_CALL(mock_base_context, setHeadersModifier(testing::_));
  context.setHeadersModifier(std::move(headers_modifier));
}

// Test cluster update callbacks.
TEST(CompositeClusterTest, ClusterUpdateCallbacks) {
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto parent_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  ON_CALL(*parent_info, name()).WillByDefault(ReturnRef(primary_name));

  auto cluster_set = std::make_shared<ClusterSet>();
  cluster_set->push_back("primary_cluster");
  cluster_set->push_back("secondary_cluster");

  CompositeClusterLoadBalancer lb(parent_info, cluster_manager, cluster_set,
                                  envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL);

  // Test onClusterAddOrUpdate for cluster in our set.
  NiceMock<Upstream::MockThreadLocalCluster> tlc;
  Upstream::ThreadLocalClusterCommand cluster_command = [&tlc]() -> Upstream::ThreadLocalCluster& {
    return tlc;
  };

  lb.onClusterAddOrUpdate("primary_cluster", cluster_command);

  // Test onClusterAddOrUpdate for cluster not in our set.
  lb.onClusterAddOrUpdate("unrelated_cluster", cluster_command);

  // Test onClusterRemoval for cluster in our set.
  lb.onClusterRemoval("primary_cluster");

  // Test onClusterRemoval for cluster not in our set.
  lb.onClusterRemoval("unrelated_cluster");
}

// Test lifetimeCallbacks method returns empty.
TEST(CompositeClusterTest, LifetimeCallbacks) {
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto parent_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  auto cluster_set = std::make_shared<ClusterSet>();
  cluster_set->push_back("primary_cluster");

  CompositeClusterLoadBalancer lb(parent_info, cluster_manager, cluster_set,
                                  envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL);

  auto callbacks = lb.lifetimeCallbacks();
  EXPECT_FALSE(callbacks.has_value());
}

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
