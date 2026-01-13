#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/composite/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/http/conn_pool.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/load_balancer.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {

class CompositeClusterTest : public testing::Test {
public:
  CompositeClusterTest() = default;

  void initialize(const std::string& yaml_config) {
    cluster_config_ = Upstream::parseClusterFromV3Yaml(yaml_config);
    THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
        cluster_config_.cluster_type().typed_config(),
        ProtobufMessage::getStrictValidationVisitor(), config_));

    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               false);

    absl::Status creation_status = absl::OkStatus();
    cluster_ = std::shared_ptr<Cluster>(
        new Cluster(cluster_config_, config_, factory_context, creation_status));
    THROW_IF_NOT_OK(creation_status);
  }

  envoy::config::cluster::v3::Cluster cluster_config_;
  envoy::extensions::clusters::composite::v3::ClusterConfig config_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  std::shared_ptr<Cluster> cluster_;
};

// Test basic cluster creation.
TEST_F(CompositeClusterTest, BasicCreation) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: primary
    - name: secondary
)EOF";

  initialize(yaml);
  EXPECT_EQ(2, cluster_->clusters_->size());
  EXPECT_EQ("primary", (*cluster_->clusters_)[0]);
  EXPECT_EQ("secondary", (*cluster_->clusters_)[1]);
  EXPECT_EQ(Upstream::Cluster::InitializePhase::Secondary, cluster_->initializePhase());
}

// Test attempt count extraction from LoadBalancerContext.
TEST_F(CompositeClusterTest, AttemptCountExtraction) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: primary
    - name: secondary
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                  cluster_->clusters_);

  // Test with null context.
  EXPECT_EQ(0, lb.getAttemptCount(nullptr));

  // Test with context but no stream info.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  EXPECT_CALL(context, requestStreamInfo()).WillOnce(Return(nullptr));
  EXPECT_EQ(0, lb.getAttemptCount(&context));

  // Test with stream info containing attempt count.
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(context, requestStreamInfo()).WillOnce(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(2));
  EXPECT_EQ(2, lb.getAttemptCount(&context));

  // Test with stream info returning nullopt.
  NiceMock<StreamInfo::MockStreamInfo> stream_info_null;
  NiceMock<Upstream::MockLoadBalancerContext> context_null;
  EXPECT_CALL(context_null, requestStreamInfo()).WillOnce(Return(&stream_info_null));
  EXPECT_CALL(stream_info_null, attemptCount()).WillOnce(Return(absl::nullopt));
  EXPECT_EQ(0, lb.getAttemptCount(&context_null));
}

// Test cluster index mapping.
TEST_F(CompositeClusterTest, ClusterIndexMapping) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: primary
    - name: secondary
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                  cluster_->clusters_);

  // Test invalid attempt 0.
  EXPECT_FALSE(lb.mapAttemptToClusterIndex(0).has_value());

  // Test normal mapping (1-based attempts).
  EXPECT_EQ(0, lb.mapAttemptToClusterIndex(1).value()); // First attempt -> first cluster.
  EXPECT_EQ(1, lb.mapAttemptToClusterIndex(2).value()); // Second attempt -> second cluster.

  // Test overflow - should fail when attempts exceed available clusters.
  EXPECT_FALSE(lb.mapAttemptToClusterIndex(3).has_value());
  EXPECT_FALSE(lb.mapAttemptToClusterIndex(10).has_value());
}

// Test getClusterByIndex with bounds checking.
TEST_F(CompositeClusterTest, GetClusterByIndexBoundsCheck) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: primary
    - name: secondary
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                  cluster_->clusters_);

  // Test out of bounds index.
  EXPECT_EQ(nullptr, lb.getClusterByIndex(2));
  EXPECT_EQ(nullptr, lb.getClusterByIndex(10));

  // Test that cluster manager returns nullptr for unknown cluster.
  EXPECT_EQ(nullptr, lb.getClusterByIndex(0)); // primary doesn't exist in cluster manager.
  EXPECT_EQ(nullptr, lb.getClusterByIndex(1)); // secondary doesn't exist in cluster manager.
}

// Test load balancer methods when no clusters are available.
TEST_F(CompositeClusterTest, LoadBalancerMethodsNoCluster) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: primary
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                  cluster_->clusters_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Upstream::MockHost> host;
  std::vector<uint8_t> hash_key;

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(1)));

  // Test all load balancer methods when no cluster is available.
  auto result = lb.chooseHost(&context);
  EXPECT_EQ(nullptr, result.host);

  EXPECT_EQ(nullptr, lb.peekAnotherHost(&context));
  EXPECT_EQ(absl::nullopt, lb.selectExistingConnection(&context, host, hash_key));
  EXPECT_FALSE(lb.lifetimeCallbacks().has_value());
}

// Test cluster update callbacks.
TEST_F(CompositeClusterTest, ClusterUpdateCallbacks) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: primary
    - name: secondary
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                  cluster_->clusters_);

  // Test cluster removal for cluster in our list and unknown cluster.
  lb.onClusterRemoval("primary");
  lb.onClusterRemoval("unknown");
}

// Test thread aware load balancer and factory classes.
TEST_F(CompositeClusterTest, ThreadAwareLoadBalancerAndFactory) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: primary
)EOF";

  initialize(yaml);

  // Test thread aware load balancer.
  CompositeThreadAwareLoadBalancer thread_aware_lb(*cluster_);
  EXPECT_NE(nullptr, thread_aware_lb.factory());
  EXPECT_TRUE(thread_aware_lb.initialize().ok());

  // Test load balancer factory.
  CompositeLoadBalancerFactory factory(*cluster_);
  NiceMock<Upstream::MockPrioritySet> priority_set;
  Upstream::LoadBalancerParams params{priority_set, nullptr};
  auto lb = factory.create(params);
  EXPECT_NE(nullptr, lb);
}

// Test load balancer context wrapper with real context.
TEST_F(CompositeClusterTest, LoadBalancerContextDelegation) {
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  NiceMock<Upstream::MockPrioritySet> priority_set;
  Upstream::HealthyAndDegradedLoad load;
  Upstream::RetryPriority::PriorityMappingFunc mapping_func;

  CompositeLoadBalancerContext wrapper(&mock_context, 1);

  // Test delegated methods.
  EXPECT_CALL(mock_context, computeHashKey()).WillOnce(Return(123));
  EXPECT_EQ(123, wrapper.computeHashKey().value());

  EXPECT_CALL(mock_context, downstreamConnection()).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, wrapper.downstreamConnection());

  EXPECT_CALL(mock_context, requestStreamInfo()).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, wrapper.requestStreamInfo());

  EXPECT_CALL(mock_context, determinePriorityLoad(_, _, _)).WillOnce(ReturnRef(load));
  wrapper.determinePriorityLoad(priority_set, load, mapping_func);

  NiceMock<Upstream::MockHost> host;
  EXPECT_CALL(mock_context, shouldSelectAnotherHost(_)).WillOnce(Return(false));
  EXPECT_FALSE(wrapper.shouldSelectAnotherHost(host));

  EXPECT_CALL(mock_context, hostSelectionRetryCount()).WillOnce(Return(3));
  EXPECT_EQ(3, wrapper.hostSelectionRetryCount());

  // Test selected cluster index.
  EXPECT_EQ(1, wrapper.selectedClusterIndex());
}

TEST_F(CompositeClusterTest, LoadBalancerContextAsyncHostSelectionDelegates) {
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  CompositeLoadBalancerContext wrapper(&mock_context, 5);

  auto expected_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  const Upstream::Host* expected_host_raw = expected_host.get();
  std::string expected_details = "async-selection-details";

  EXPECT_CALL(mock_context, onAsyncHostSelection(_, _))
      .WillOnce([expected_host_raw, &expected_details](Upstream::HostConstSharedPtr&& received_host,
                                                       std::string&& received_details) {
        EXPECT_EQ(expected_host_raw, received_host.get());
        EXPECT_EQ(expected_details, received_details);
      });

  wrapper.onAsyncHostSelection(expected_host, std::move(expected_details));
}

// Test load balancer context wrapper with null context (owned context path).
TEST_F(CompositeClusterTest, LoadBalancerContextWithNullContext) {
  CompositeLoadBalancerContext wrapper(nullptr, 0);

  // Should create owned context and delegate to it.
  EXPECT_EQ(absl::nullopt, wrapper.computeHashKey());
  EXPECT_EQ(nullptr, wrapper.downstreamConnection());
  EXPECT_EQ(nullptr, wrapper.requestStreamInfo());

  NiceMock<Upstream::MockHost> host;
  EXPECT_FALSE(wrapper.shouldSelectAnotherHost(host));
  EXPECT_EQ(1, wrapper.hostSelectionRetryCount()); // LoadBalancerContextBase returns 1 by default.

  EXPECT_EQ(0, wrapper.selectedClusterIndex());
}

// Test cluster constructor when some thread local clusters don't exist.
TEST_F(CompositeClusterTest, ConstructorWithMissingClusters) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: missing_cluster
    - name: another_missing_cluster
)EOF";

  // This should still construct successfully even if clusters don't exist yet.
  initialize(yaml);
  EXPECT_EQ(2, cluster_->clusters_->size());
  EXPECT_EQ("missing_cluster", (*cluster_->clusters_)[0]);
  EXPECT_EQ("another_missing_cluster", (*cluster_->clusters_)[1]);
}

// Test cluster update callbacks when clusters are added/updated.
TEST_F(CompositeClusterTest, ClusterUpdateCallbacksAddUpdate) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: primary
    - name: secondary
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                  cluster_->clusters_);

  // Test onClusterAddOrUpdate for clusters in our list.
  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockThreadLocalCluster>* mock_cluster_ptr = &mock_cluster;

  auto get_cluster_func = [mock_cluster_ptr]() -> Upstream::ThreadLocalCluster& {
    return *mock_cluster_ptr;
  };

  Upstream::ThreadLocalClusterCommand command(get_cluster_func);

  lb.onClusterAddOrUpdate("primary", command);
  lb.onClusterAddOrUpdate("unknown_cluster", command); // Should be ignored.
}

// Test cluster factory name.
TEST_F(CompositeClusterTest, ClusterFactoryName) {
  ClusterFactory factory;
  EXPECT_EQ("envoy.clusters.composite", factory.name());
}

// Test successful host selection with delegation to sub-cluster.
TEST_F(CompositeClusterTest, ChooseHostSuccessfulDelegation) {
  initialize(R"EOF(
name: delegation_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: target_cluster
)EOF");

  CompositeClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                  cluster_->clusters_);

  // Set up mocks for successful delegation.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockLoadBalancer> mock_lb;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto cluster_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(1)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("target_cluster"))
      .WillRepeatedly(Return(&mock_cluster));
  EXPECT_CALL(mock_cluster, loadBalancer()).WillRepeatedly(ReturnRef(mock_lb));
  EXPECT_CALL(mock_cluster, info()).WillRepeatedly(Return(cluster_info));
  std::string target_cluster_name = "target_cluster";
  EXPECT_CALL(*cluster_info, name()).WillRepeatedly(ReturnRef(target_cluster_name));
  EXPECT_CALL(mock_lb, chooseHost(_)).WillOnce(Return(Upstream::HostSelectionResponse{mock_host}));

  auto result = lb.chooseHost(&context);
  EXPECT_EQ(mock_host, result.host);
}

// Test peekAnotherHost with successful delegation.
TEST_F(CompositeClusterTest, PeekAnotherHostSuccessfulDelegation) {
  initialize(R"EOF(
name: peek_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: peek_target
)EOF");

  CompositeClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                  cluster_->clusters_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockLoadBalancer> mock_lb;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(1)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("peek_target"))
      .WillOnce(Return(&mock_cluster));
  EXPECT_CALL(mock_cluster, loadBalancer()).WillOnce(ReturnRef(mock_lb));
  EXPECT_CALL(mock_lb, peekAnotherHost(_)).WillOnce(Return(mock_host));

  auto result = lb.peekAnotherHost(&context);
  EXPECT_EQ(mock_host, result);
}

TEST_F(CompositeClusterTest, PeekAnotherHostReturnsNullptrWhenAttemptExceedsClusters) {
  const std::string yaml = R"EOF(
name: overflow_peek_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: primary
    - name: secondary
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                  cluster_->clusters_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(3)));

  EXPECT_EQ(nullptr, lb.peekAnotherHost(&context));
}

TEST_F(CompositeClusterTest, PeekAnotherHostReturnsNullptrWhenClusterUnavailable) {
  const std::string yaml = R"EOF(
name: missing_peek_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: missing_cluster
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                  cluster_->clusters_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(1)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("missing_cluster"))
      .WillOnce(Return(nullptr));

  EXPECT_EQ(nullptr, lb.peekAnotherHost(&context));
}

// Test selectExistingConnection with successful delegation.
TEST_F(CompositeClusterTest, SelectExistingConnectionSuccessfulDelegation) {
  initialize(R"EOF(
name: select_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: select_target
)EOF");

  CompositeClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                  cluster_->clusters_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockLoadBalancer> mock_lb;
  NiceMock<Upstream::MockHost> mock_host;

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(1)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("select_target"))
      .WillOnce(Return(&mock_cluster));
  EXPECT_CALL(mock_cluster, loadBalancer()).WillOnce(ReturnRef(mock_lb));

  std::vector<uint8_t> hash_key;
  NiceMock<Http::ConnectionPool::MockInstance> mock_pool;
  NiceMock<Network::MockConnection> mock_connection;
  Upstream::SelectedPoolAndConnection expected_result{mock_pool, mock_connection};
  EXPECT_CALL(mock_lb, selectExistingConnection(_, _, _))
      .WillOnce(Return(absl::optional<Upstream::SelectedPoolAndConnection>(expected_result)));

  auto connection_result = lb.selectExistingConnection(&context, mock_host, hash_key);
  EXPECT_TRUE(connection_result.has_value());
}

// Test factory create method.
TEST_F(CompositeClusterTest, FactoryCreateMethod) {
  ClusterFactory factory;
  envoy::config::cluster::v3::Cluster cluster_config;
  Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                             false);

  cluster_config.set_name("test_factory_cluster");
  cluster_config.mutable_connect_timeout()->set_seconds(5);
  cluster_config.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

  auto* cluster_type = cluster_config.mutable_cluster_type();
  cluster_type->set_name("envoy.clusters.composite");
  envoy::extensions::clusters::composite::v3::ClusterConfig typed_config;
  auto* entry = typed_config.add_clusters();
  entry->set_name("factory_cluster_1");
  cluster_type->mutable_typed_config()->PackFrom(typed_config);

  auto result = factory.create(cluster_config, factory_context);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(nullptr, result.value().first);
  EXPECT_NE(nullptr, result.value().second);
}

// Test LoadBalancerContext additional methods.
TEST_F(CompositeClusterTest, LoadBalancerContextAdditionalMethods) {
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  CompositeLoadBalancerContext wrapper(&mock_context, 2);

  // Test metadataMatchCriteria.
  NiceMock<Router::MockMetadataMatchCriteria> criteria;
  EXPECT_CALL(mock_context, metadataMatchCriteria()).WillOnce(Return(&criteria));
  EXPECT_EQ(&criteria, wrapper.metadataMatchCriteria());

  // Test overrideHostToSelect.
  absl::optional<std::pair<absl::string_view, bool>> override_host =
      std::make_pair("override_host", true);
  EXPECT_CALL(mock_context, overrideHostToSelect()).WillOnce(Return(override_host));
  EXPECT_EQ(override_host, wrapper.overrideHostToSelect());

  // Test setHeadersModifier.
  std::function<void(Http::ResponseHeaderMap&)> modifier;
  EXPECT_CALL(mock_context, setHeadersModifier(_));
  wrapper.setHeadersModifier(std::move(modifier));

  // Test downstreamHeaders.
  const Http::RequestHeaderMap* headers = nullptr;
  EXPECT_CALL(mock_context, downstreamHeaders()).WillOnce(Return(headers));
  EXPECT_EQ(headers, wrapper.downstreamHeaders());

  // Test upstreamSocketOptions.
  Network::Socket::OptionsSharedPtr socket_options;
  EXPECT_CALL(mock_context, upstreamSocketOptions()).WillOnce(Return(socket_options));
  EXPECT_EQ(socket_options, wrapper.upstreamSocketOptions());

  // Test upstreamTransportSocketOptions.
  Network::TransportSocketOptionsConstSharedPtr transport_options;
  EXPECT_CALL(mock_context, upstreamTransportSocketOptions()).WillOnce(Return(transport_options));
  EXPECT_EQ(transport_options, wrapper.upstreamTransportSocketOptions());
}

// Test chooseHost with missing cluster.
TEST_F(CompositeClusterTest, ChooseHostWithMissingCluster) {
  const std::string yaml = R"EOF(
name: missing_cluster_test
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: missing_cluster_0
    - name: missing_cluster_1
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                  cluster_->clusters_);

  // Mock context for attempt 1 (should map to cluster index 0).
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(mock_context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(1)));

  // Mock cluster manager to return nullptr for missing_cluster_0.
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("missing_cluster_0"))
      .WillOnce(Return(nullptr));

  // chooseHost should return nullptr when cluster is not found.
  auto result = lb.chooseHost(&mock_context);
  EXPECT_EQ(nullptr, result.host);
}

// Test overflow behavior when attempts exceed available clusters.
TEST_F(CompositeClusterTest, OverflowBehavior) {
  const std::string yaml = R"EOF(
name: overflow_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: cluster1
    - name: cluster2
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                  cluster_->clusters_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));

  // Test attempt 3 which exceeds available clusters (only have 2).
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(3)));

  auto result = lb.chooseHost(&context);
  EXPECT_EQ(nullptr, result.host);
}

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
