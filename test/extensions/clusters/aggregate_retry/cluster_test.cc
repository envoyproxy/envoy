#include "envoy/extensions/clusters/aggregate_retry/v3/cluster.pb.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/aggregate_retry/cluster.h"

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
namespace AggregateRetry {

class AggregateRetryClusterTest : public testing::Test {
public:
  AggregateRetryClusterTest() = default;

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
  envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig config_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  std::shared_ptr<Cluster> cluster_;
};

// Test basic cluster creation with FAIL overflow behavior (default).
TEST_F(AggregateRetryClusterTest, BasicCreation) {
  const std::string yaml = R"EOF(
name: aggregate_retry_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - primary
    - secondary
)EOF";

  initialize(yaml);
  EXPECT_EQ(2, cluster_->clusters_->size());
  EXPECT_EQ("primary", (*cluster_->clusters_)[0]);
  EXPECT_EQ("secondary", (*cluster_->clusters_)[1]);
  EXPECT_EQ(envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig::FAIL,
            cluster_->retry_overflow_behavior_);
  EXPECT_EQ(Upstream::Cluster::InitializePhase::Secondary, cluster_->initializePhase());
}

// Test configuration with USE_LAST_CLUSTER overflow behavior.
TEST_F(AggregateRetryClusterTest, UseLastClusterBehavior) {
  const std::string yaml = R"EOF(
name: aggregate_retry_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - primary
    - secondary
    - tertiary
    retry_overflow_behavior: USE_LAST_CLUSTER
)EOF";

  initialize(yaml);
  EXPECT_EQ(3, cluster_->clusters_->size());
  EXPECT_EQ(envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig::USE_LAST_CLUSTER,
            cluster_->retry_overflow_behavior_);
}

// Test retry attempt extraction from LoadBalancerContext.
TEST_F(AggregateRetryClusterTest, RetryAttemptExtraction) {
  const std::string yaml = R"EOF(
name: aggregate_retry_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - primary
    - secondary
)EOF";

  initialize(yaml);

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  // Test with null context.
  EXPECT_EQ(0, lb.getRetryAttemptCount(nullptr));

  // Test with context but no stream info.
  testing::NiceMock<Upstream::MockLoadBalancerContext> context;
  EXPECT_CALL(context, requestStreamInfo()).WillOnce(Return(nullptr));
  EXPECT_EQ(0, lb.getRetryAttemptCount(&context));

  // Test with stream info containing attempt count.
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(context, requestStreamInfo()).WillOnce(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(2));
  EXPECT_EQ(2, lb.getRetryAttemptCount(&context));

  // Test with stream info returning nullopt.
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_null;
  testing::NiceMock<Upstream::MockLoadBalancerContext> context_null;
  EXPECT_CALL(context_null, requestStreamInfo()).WillOnce(Return(&stream_info_null));
  EXPECT_CALL(stream_info_null, attemptCount()).WillOnce(Return(absl::nullopt));
  EXPECT_EQ(0, lb.getRetryAttemptCount(&context_null));
}

// Test cluster index mapping with FAIL overflow behavior.
TEST_F(AggregateRetryClusterTest, ClusterIndexMappingFail) {
  const std::string yaml = R"EOF(
name: aggregate_retry_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - primary
    - secondary
    retry_overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  // Test normal mapping.
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(0));
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(1));

  // Test overflow with FAIL behavior.
  EXPECT_EQ(2, lb.mapRetryAttemptToClusterIndex(2));  // Out of bounds index
  EXPECT_EQ(2, lb.mapRetryAttemptToClusterIndex(10)); // Far out of bounds
}

// Test cluster index mapping with USE_LAST_CLUSTER overflow behavior.
TEST_F(AggregateRetryClusterTest, ClusterIndexMappingUseLastCluster) {
  const std::string yaml = R"EOF(
name: aggregate_retry_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - primary
    - secondary
    retry_overflow_behavior: USE_LAST_CLUSTER
)EOF";

  initialize(yaml);

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  // Test normal mapping.
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(0));
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(1));

  // Test overflow with USE_LAST_CLUSTER behavior.
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(2));  // Should use last cluster (index 1)
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(10)); // Should still use last cluster
}

// Test getClusterByIndex with bounds checking.
TEST_F(AggregateRetryClusterTest, GetClusterByIndex) {
  const std::string yaml = R"EOF(
name: aggregate_retry_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - primary
    - secondary
)EOF";

  initialize(yaml);

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  // Test out of bounds index.
  EXPECT_EQ(nullptr, lb.getClusterByIndex(2));
  EXPECT_EQ(nullptr, lb.getClusterByIndex(10));

  // Test that cluster manager returns nullptr for unknown cluster.
  EXPECT_EQ(nullptr, lb.getClusterByIndex(0)); // primary doesn't exist in cluster manager
  EXPECT_EQ(nullptr, lb.getClusterByIndex(1)); // secondary doesn't exist in cluster manager
}

// Test load balancer methods when no clusters are available.
TEST_F(AggregateRetryClusterTest, LoadBalancerMethodsNoCluster) {
  const std::string yaml = R"EOF(
name: aggregate_retry_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - primary
)EOF";

  initialize(yaml);

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  testing::NiceMock<Upstream::MockLoadBalancerContext> context;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  testing::NiceMock<Upstream::MockHost> host;
  std::vector<uint8_t> hash_key;

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(0));

  // Test all load balancer methods when no cluster is available.
  auto result = lb.chooseHost(&context);
  EXPECT_EQ(nullptr, result.host);

  EXPECT_EQ(nullptr, lb.peekAnotherHost(&context));
  EXPECT_EQ(absl::nullopt, lb.selectExistingConnection(&context, host, hash_key));
  EXPECT_FALSE(lb.lifetimeCallbacks().has_value());
}

// Test cluster update and removal callbacks.
TEST_F(AggregateRetryClusterTest, ClusterUpdateCallbacks) {
  const std::string yaml = R"EOF(
name: aggregate_retry_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - primary
    - secondary
)EOF";

  initialize(yaml);

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  // Test cluster removal for cluster in our list and unknown cluster.
  lb.onClusterRemoval("primary");
  lb.onClusterRemoval("unknown");
}

// Test thread aware load balancer and factory classes.
TEST_F(AggregateRetryClusterTest, ThreadAwareLoadBalancerAndFactory) {
  const std::string yaml = R"EOF(
name: aggregate_retry_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - primary
)EOF";

  initialize(yaml);

  // Test thread aware load balancer.
  AggregateRetryThreadAwareLoadBalancer thread_aware_lb(*cluster_);
  EXPECT_NE(nullptr, thread_aware_lb.factory());
  EXPECT_TRUE(thread_aware_lb.initialize().ok());

  // Test load balancer factory.
  AggregateRetryLoadBalancerFactory factory(*cluster_);
  testing::NiceMock<Upstream::MockPrioritySet> priority_set;
  Upstream::LoadBalancerParams params{priority_set, nullptr};
  auto lb = factory.create(params);
  EXPECT_NE(nullptr, lb);
}

// Test load balancer context wrapper with real context.
TEST_F(AggregateRetryClusterTest, LoadBalancerContext) {
  testing::NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  testing::NiceMock<Upstream::MockPrioritySet> priority_set;
  Upstream::HealthyAndDegradedLoad load;
  Upstream::RetryPriority::PriorityMappingFunc mapping_func;

  AggregateRetryLoadBalancerContext wrapper(&mock_context, 1);

  // Test key delegated methods.
  EXPECT_CALL(mock_context, computeHashKey()).WillOnce(Return(123));
  EXPECT_EQ(123, wrapper.computeHashKey().value());

  EXPECT_CALL(mock_context, downstreamConnection()).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, wrapper.downstreamConnection());

  EXPECT_CALL(mock_context, requestStreamInfo()).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, wrapper.requestStreamInfo());

  EXPECT_CALL(mock_context, determinePriorityLoad(_, _, _)).WillOnce(ReturnRef(load));
  wrapper.determinePriorityLoad(priority_set, load, mapping_func);

  testing::NiceMock<Upstream::MockHost> host;
  EXPECT_CALL(mock_context, shouldSelectAnotherHost(_)).WillOnce(Return(false));
  EXPECT_FALSE(wrapper.shouldSelectAnotherHost(host));

  EXPECT_CALL(mock_context, hostSelectionRetryCount()).WillOnce(Return(3));
  EXPECT_EQ(3, wrapper.hostSelectionRetryCount());

  // Test selected cluster index.
  EXPECT_EQ(1, wrapper.selectedClusterIndex());
}

// Test load balancer context wrapper with null context (owned context path).
TEST_F(AggregateRetryClusterTest, LoadBalancerContextWithNullContext) {
  AggregateRetryLoadBalancerContext wrapper(nullptr, 0);

  // Should create owned context and delegate to it.
  EXPECT_EQ(absl::nullopt, wrapper.computeHashKey());
  EXPECT_EQ(nullptr, wrapper.downstreamConnection());
  EXPECT_EQ(nullptr, wrapper.requestStreamInfo());

  testing::NiceMock<Upstream::MockHost> host;
  EXPECT_FALSE(wrapper.shouldSelectAnotherHost(host));
  EXPECT_EQ(1, wrapper.hostSelectionRetryCount()); // LoadBalancerContextBase returns 1 by default

  EXPECT_EQ(0, wrapper.selectedClusterIndex());
}

// Test LoadBalancerContext additional methods for coverage.
TEST_F(AggregateRetryClusterTest, LoadBalancerContextAdditionalMethods) {
  testing::NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  AggregateRetryLoadBalancerContext wrapper(&mock_context, 2);

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

  // Test onAsyncHostSelection with correct signature.
  Upstream::HostConstSharedPtr host;
  std::string details = "test";
  EXPECT_CALL(mock_context, onAsyncHostSelection(_, _));
  wrapper.onAsyncHostSelection(std::move(host), std::move(details));
}

// Test cluster constructor when some thread local clusters don't exist.
TEST_F(AggregateRetryClusterTest, ConstructorWithMissingClusters) {
  const std::string yaml = R"EOF(
name: aggregate_retry_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - missing_cluster
    - another_missing_cluster
)EOF";

  // This should still construct successfully even if clusters don't exist yet.
  initialize(yaml);
  EXPECT_EQ(2, cluster_->clusters_->size());
  EXPECT_EQ("missing_cluster", (*cluster_->clusters_)[0]);
  EXPECT_EQ("another_missing_cluster", (*cluster_->clusters_)[1]);
}

// Test cluster update callbacks when clusters are added/updated.
TEST_F(AggregateRetryClusterTest, ClusterUpdateCallbacksAddUpdate) {
  const std::string yaml = R"EOF(
name: aggregate_retry_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - primary
    - secondary
)EOF";

  initialize(yaml);

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  // Test onClusterAddOrUpdate for clusters in our list.
  testing::NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  testing::NiceMock<Upstream::MockThreadLocalCluster>* mock_cluster_ptr = &mock_cluster;

  auto get_cluster_func = [mock_cluster_ptr]() -> Upstream::ThreadLocalCluster& {
    return *mock_cluster_ptr;
  };

  Upstream::ThreadLocalClusterCommand command(get_cluster_func);

  lb.onClusterAddOrUpdate("primary", command);
  lb.onClusterAddOrUpdate("unknown_cluster", command); // Should be ignored
}

// Test cluster factory name.
TEST_F(AggregateRetryClusterTest, ClusterFactoryName) {
  ClusterFactory factory;
  EXPECT_EQ("envoy.clusters.aggregate_retry", factory.name());
}

// Test onClusterRemoval with tracked and untracked clusters.
TEST_F(AggregateRetryClusterTest, OnClusterRemovalCoverage) {
  const std::string yaml = R"EOF(
name: removal_test_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - cluster_a
    - cluster_b
    - cluster_c
)EOF";

  initialize(yaml);

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  // Test removal of clusters that are in our list (should trigger refresh).
  lb.onClusterRemoval("cluster_a");
  lb.onClusterRemoval("cluster_c");

  // Test removal of clusters not in our list (should be ignored).
  lb.onClusterRemoval("unknown_cluster");
  lb.onClusterRemoval("not_tracked");
}

// Test ClusterFactory create method through public interface.
TEST_F(AggregateRetryClusterTest, FactoryCreatePublicInterface) {
  ClusterFactory factory;
  envoy::config::cluster::v3::Cluster cluster_config = Upstream::parseClusterFromV3Yaml(R"EOF(
    name: test_factory_cluster
    cluster_type:
      name: envoy.clusters.aggregate_retry
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
        clusters: ["test_cluster_1"]
        retry_overflow_behavior: FAIL
  )EOF");

  Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                             false);

  auto result = factory.create(cluster_config, factory_context);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(nullptr, result.value().first);
  EXPECT_NE(nullptr, result.value().second);
}

// Test lifetimeCallbacks method which should return empty.
TEST_F(AggregateRetryClusterTest, LifetimeCallbacksReturnsEmpty) {
  initialize(R"EOF(
    name: lifetime_test_cluster
    cluster_type:
      name: envoy.clusters.aggregate_retry
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
        clusters: ["primary"]
        retry_overflow_behavior: FAIL
  )EOF");

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  auto callbacks = lb.lifetimeCallbacks();
  EXPECT_FALSE(callbacks.has_value());
}

// Test getClusterByIndex with out-of-bounds index to cover debug logging.
TEST_F(AggregateRetryClusterTest, GetClusterByIndexOutOfBounds) {
  const std::string yaml = R"EOF(
name: bounds_test_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - cluster1
    - cluster2
)EOF";

  initialize(yaml);

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  auto cluster = lb.getClusterByIndex(5);
  EXPECT_EQ(nullptr, cluster);

  // Test with index equal to cluster size.
  cluster = lb.getClusterByIndex(2);
  EXPECT_EQ(nullptr, cluster);
}

TEST_F(AggregateRetryClusterTest, GetClusterByIndexClusterNotFound) {
  const std::string yaml = R"EOF(
name: not_found_test_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - missing_cluster
)EOF";

  initialize(yaml);

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("missing_cluster"))
      .WillOnce(Return(nullptr));

  auto cluster = lb.getClusterByIndex(0);
  EXPECT_EQ(nullptr, cluster);
}

// Test default case in switch statement for retry overflow behavior.
TEST_F(AggregateRetryClusterTest, RetryOverflowDefaultCase) {
  initialize(R"EOF(
    name: default_case_cluster
    cluster_type:
      name: envoy.clusters.aggregate_retry
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
        clusters: ["cluster1", "cluster2"]
        retry_overflow_behavior: FAIL
  )EOF");

  AggregateRetryClusterLoadBalancer lb(
      cluster_->info(), server_context_.cluster_manager_, cluster_->clusters_,
      static_cast<
          envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig::RetryOverflowBehavior>(
          999));

  // Test overflow condition to trigger default case.
  size_t index = lb.mapRetryAttemptToClusterIndex(10);
  EXPECT_EQ(2, index); // Should default to FAIL behavior
}

// Test AggregateRetryThreadAwareLoadBalancer initialization.
TEST_F(AggregateRetryClusterTest, ThreadAwareLoadBalancerInitialization) {
  const std::string yaml = R"EOF(
name: thread_aware_test
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.aggregate_retry
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
    clusters:
    - test_cluster
)EOF";

  initialize(yaml);

  AggregateRetryThreadAwareLoadBalancer thread_aware_lb(*cluster_);

  // Test initialization method (should return OK status).
  auto status = thread_aware_lb.initialize();
  EXPECT_TRUE(status.ok());

  // Test factory retrieval.
  auto factory = thread_aware_lb.factory();
  EXPECT_NE(nullptr, factory);
}

// Test edge case for mapRetryAttemptToClusterIndex boundary conditions.
TEST_F(AggregateRetryClusterTest, MapRetryAttemptBoundaryConditions) {
  initialize(R"EOF(
    name: boundary_test_cluster
    cluster_type:
      name: envoy.clusters.aggregate_retry
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
        clusters: ["first", "second", "third"]
        retry_overflow_behavior: USE_LAST_CLUSTER
  )EOF");

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  // Test exact boundary conditions.
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(0));   // First cluster
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(1));   // Second cluster
  EXPECT_EQ(2, lb.mapRetryAttemptToClusterIndex(2));   // Third cluster
  EXPECT_EQ(2, lb.mapRetryAttemptToClusterIndex(3));   // Overflow, use last
  EXPECT_EQ(2, lb.mapRetryAttemptToClusterIndex(100)); // Large overflow, use last
}

TEST_F(AggregateRetryClusterTest, AddMemberUpdateCallbackForExistingCluster) {
  initialize(R"EOF(
    name: callback_cluster
    cluster_type:
      name: envoy.clusters.aggregate_retry
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
        clusters: ["existing_cluster"]
        retry_overflow_behavior: FAIL
  )EOF");

  testing::NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  testing::NiceMock<Upstream::MockPrioritySet> priority_set;
  auto cluster_info = std::make_shared<testing::NiceMock<Upstream::MockClusterInfo>>();

  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("existing_cluster"))
      .WillRepeatedly(Return(&mock_cluster));
  EXPECT_CALL(mock_cluster, prioritySet()).WillRepeatedly(ReturnRef(priority_set));
  EXPECT_CALL(mock_cluster, info()).WillRepeatedly(Return(cluster_info));
  std::string cluster_name = "existing_cluster";
  EXPECT_CALL(*cluster_info, name()).WillRepeatedly(ReturnRef(cluster_name));
  EXPECT_CALL(priority_set, addMemberUpdateCb(_)).WillOnce(Return(nullptr));

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);
}

TEST_F(AggregateRetryClusterTest, ChooseHostSuccessfulDelegation) {
  initialize(R"EOF(
    name: delegation_cluster
    cluster_type:
      name: envoy.clusters.aggregate_retry
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
        clusters: ["target_cluster"]
        retry_overflow_behavior: FAIL
  )EOF");

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  // Set up mocks for successful delegation.
  testing::NiceMock<Upstream::MockLoadBalancerContext> context;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  testing::NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  testing::NiceMock<Upstream::MockLoadBalancer> mock_lb;
  auto mock_host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
  auto cluster_info = std::make_shared<testing::NiceMock<Upstream::MockClusterInfo>>();

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(0)));
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

TEST_F(AggregateRetryClusterTest, PeekAnotherHostSuccessfulDelegation) {
  initialize(R"EOF(
    name: peek_cluster
    cluster_type:
      name: envoy.clusters.aggregate_retry
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
        clusters: ["peek_target"]
        retry_overflow_behavior: FAIL
  )EOF");

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  testing::NiceMock<Upstream::MockLoadBalancerContext> context;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  testing::NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  testing::NiceMock<Upstream::MockLoadBalancer> mock_lb;
  auto mock_host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(0)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("peek_target"))
      .WillOnce(Return(&mock_cluster));
  EXPECT_CALL(mock_cluster, loadBalancer()).WillOnce(ReturnRef(mock_lb));
  EXPECT_CALL(mock_lb, peekAnotherHost(_)).WillOnce(Return(mock_host));

  auto result = lb.peekAnotherHost(&context);
  EXPECT_EQ(mock_host, result);
}

TEST_F(AggregateRetryClusterTest, SelectExistingConnectionSuccessfulDelegation) {
  initialize(R"EOF(
    name: select_cluster
    cluster_type:
      name: envoy.clusters.aggregate_retry
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
        clusters: ["select_target"]
        retry_overflow_behavior: FAIL
  )EOF");

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);

  testing::NiceMock<Upstream::MockLoadBalancerContext> context;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  testing::NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  testing::NiceMock<Upstream::MockLoadBalancer> mock_lb;
  testing::NiceMock<Upstream::MockHost> mock_host;

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(0)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("select_target"))
      .WillOnce(Return(&mock_cluster));
  EXPECT_CALL(mock_cluster, loadBalancer()).WillOnce(ReturnRef(mock_lb));

  std::vector<uint8_t> hash_key;
  testing::NiceMock<Http::ConnectionPool::MockInstance> mock_pool;
  testing::NiceMock<Network::MockConnection> mock_connection;
  Upstream::SelectedPoolAndConnection result{mock_pool, mock_connection};
  EXPECT_CALL(mock_lb, selectExistingConnection(_, _, _))
      .WillOnce(Return(absl::optional<Upstream::SelectedPoolAndConnection>(result)));

  auto connection_result = lb.selectExistingConnection(&context, mock_host, hash_key);
  EXPECT_TRUE(connection_result.has_value());
}

TEST_F(AggregateRetryClusterTest, ClusterGetterMethods) {
  initialize(R"EOF(
    name: getter_cluster
    cluster_type:
      name: envoy.clusters.aggregate_retry
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
        clusters: ["test_cluster"]
        retry_overflow_behavior: FAIL
  )EOF");

  EXPECT_NE(nullptr, &cluster_->runtime());
  EXPECT_NE(nullptr, &cluster_->random());
}

TEST_F(AggregateRetryClusterTest, LoadBalancerContextMethods) {
  testing::NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  AggregateRetryLoadBalancerContext wrapper(&mock_context, 2);

  testing::NiceMock<Router::MockMetadataMatchCriteria> criteria;
  EXPECT_CALL(mock_context, metadataMatchCriteria()).WillOnce(Return(&criteria));
  EXPECT_EQ(&criteria, wrapper.metadataMatchCriteria());

  absl::optional<std::pair<absl::string_view, bool>> override_host =
      std::make_pair("override_host", true);
  EXPECT_CALL(mock_context, overrideHostToSelect()).WillOnce(Return(override_host));
  EXPECT_EQ(override_host, wrapper.overrideHostToSelect());

  std::function<void(Http::ResponseHeaderMap&)> modifier;
  EXPECT_CALL(mock_context, setHeadersModifier(_));
  wrapper.setHeadersModifier(std::move(modifier));
}

TEST_F(AggregateRetryClusterTest, MemberUpdateCallbackTriggered) {
  initialize(R"EOF(
    name: member_update_cluster
    cluster_type:
      name: envoy.clusters.aggregate_retry
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
        clusters: ["callback_cluster"]
        retry_overflow_behavior: FAIL
  )EOF");

  testing::NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  testing::NiceMock<Upstream::MockPrioritySet> priority_set;
  auto cluster_info = std::make_shared<testing::NiceMock<Upstream::MockClusterInfo>>();
  Envoy::Common::CallbackHandlePtr callback_handle;

  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("callback_cluster"))
      .WillRepeatedly(Return(&mock_cluster));
  EXPECT_CALL(mock_cluster, prioritySet()).WillRepeatedly(ReturnRef(priority_set));
  EXPECT_CALL(mock_cluster, info()).WillRepeatedly(Return(cluster_info));
  std::string callback_cluster_name = "callback_cluster";
  EXPECT_CALL(*cluster_info, name()).WillRepeatedly(ReturnRef(callback_cluster_name));
  EXPECT_CALL(priority_set, addMemberUpdateCb(_))
      .WillOnce(
          [&](std::function<absl::Status(const Upstream::HostVector&, const Upstream::HostVector&)>
                  cb) {
            Upstream::HostVector added_hosts, removed_hosts;
            auto status = cb(added_hosts, removed_hosts);
            (void)status;
            return nullptr;
          });

  AggregateRetryClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                       cluster_->clusters_, cluster_->retry_overflow_behavior_);
}

TEST_F(AggregateRetryClusterTest, FactoryCreateMethodCoverage) {
  ClusterFactory factory;
  envoy::config::cluster::v3::Cluster cluster_config;
  Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                             false);

  cluster_config.set_name("test_factory_cluster");
  cluster_config.mutable_connect_timeout()->set_seconds(5);
  cluster_config.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

  auto* cluster_type = cluster_config.mutable_cluster_type();
  cluster_type->set_name("envoy.clusters.aggregate_retry");
  envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig typed_config;
  typed_config.add_clusters("factory_cluster_1");
  typed_config.add_clusters("factory_cluster_2");
  typed_config.set_retry_overflow_behavior(
      envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig::FAIL);
  cluster_type->mutable_typed_config()->PackFrom(typed_config);

  auto result = factory.create(cluster_config, factory_context);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(nullptr, result.value().first);
  EXPECT_NE(nullptr, result.value().second);
}

} // namespace AggregateRetry
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
