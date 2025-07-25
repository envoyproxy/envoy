#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/composite/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/http/conn_pool.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster.h"
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

  void initialize(const std::string& yaml) {
    const auto config = TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(yaml);
    const auto typed_config =
        TestUtility::anyConvert<envoy::extensions::clusters::composite::v3::CompositeCluster>(
            config.cluster_type().typed_config());

    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               false);
    absl::Status creation_status = absl::OkStatus();
    cluster_ = std::make_unique<Cluster>(config, typed_config, factory_context, creation_status);
    if (!creation_status.ok()) {
      ENVOY_LOG_MISC(error, "Cluster creation failed: {}", creation_status.message());
    }
    EXPECT_TRUE(creation_status.ok()) << "Error: " << creation_status.message();
    cluster_manager_ = &server_context_.cluster_manager_;

    // Create thread-aware load balancer for CLUSTER_PROVIDED policy.
    thread_aware_lb_ = std::make_unique<CompositeThreadAwareLoadBalancer>(*cluster_);
    lb_factory_ = thread_aware_lb_->factory();
    lb_ = lb_factory_->create(lb_params_);
  }

protected:
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  std::unique_ptr<Cluster> cluster_;
  Upstream::ClusterManager* cluster_manager_;
  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
  Upstream::LoadBalancerFactorySharedPtr lb_factory_;
  Upstream::LoadBalancerPtr lb_;

  // Just use this as parameters of create() method but thread aware load balancer will not use it.
  NiceMock<Upstream::MockPrioritySet> worker_priority_set_;
  Upstream::LoadBalancerParams lb_params_{worker_priority_set_, {}};
};

// Test basic cluster creation and configuration parsing.
TEST_F(CompositeClusterTest, BasicCreation) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);
  EXPECT_NE(nullptr, cluster_);
  EXPECT_EQ(2, cluster_->subClusters()->size());
  EXPECT_EQ("cluster_0", (*cluster_->subClusters())[0]);
  EXPECT_EQ("cluster_1", (*cluster_->subClusters())[1]);
  EXPECT_EQ(envoy::extensions::clusters::composite::v3::CompositeCluster::RETRY, cluster_->mode());
  EXPECT_EQ(envoy::extensions::clusters::composite::v3::CompositeCluster::RetryConfig::FAIL,
            cluster_->retryConfig().overflow_behavior());
  EXPECT_TRUE(cluster_->honorRouteRetryPolicy());
  EXPECT_EQ("composite_cluster", cluster_->name());
}

// Test USE_LAST_CLUSTER overflow behavior.
TEST_F(CompositeClusterTest, UseLastClusterBehavior) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    retry_config:
      overflow_behavior: USE_LAST_CLUSTER
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  // Test normal mapping (1-based retry attempts).
  EXPECT_FALSE(lb.mapRetryAttemptToClusterIndex(0).has_value()); // Invalid attempt 0
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(1).value()); // First attempt -> first sub-cluster
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(2).value()); // Second attempt -> second sub-cluster

  // Test overflow with USE_LAST_CLUSTER behavior.
  EXPECT_EQ(1,
            lb.mapRetryAttemptToClusterIndex(3).value()); // Should use last sub-cluster (index 1)
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(10).value()); // Should still use last sub-cluster
}

// Test ROUND_ROBIN overflow behavior.
TEST_F(CompositeClusterTest, RoundRobinOverflowBehavior) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    - name: cluster_2
    retry_config:
      overflow_behavior: ROUND_ROBIN
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  // Test normal mapping.
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(1).value()); // First attempt -> cluster 0
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(2).value()); // Second attempt -> cluster 1
  EXPECT_EQ(2, lb.mapRetryAttemptToClusterIndex(3).value()); // Third attempt -> cluster 2

  // Test round-robin overflow behavior.
  EXPECT_EQ(
      0, lb.mapRetryAttemptToClusterIndex(4).value()); // Fourth attempt -> cluster 0 (round-robin)
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(5).value()); // Fifth attempt -> cluster 1
  EXPECT_EQ(2, lb.mapRetryAttemptToClusterIndex(6).value()); // Sixth attempt -> cluster 2
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(7).value()); // Seventh attempt -> cluster 0 again
}

// Test FAIL overflow behavior.
TEST_F(CompositeClusterTest, FailOverflowBehavior) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  // Test normal mapping.
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(1).value()); // First attempt -> cluster 0
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(2).value()); // Second attempt -> cluster 1

  // Test fail overflow behavior.
  EXPECT_FALSE(lb.mapRetryAttemptToClusterIndex(3).has_value()); // Third attempt should fail
  EXPECT_FALSE(lb.mapRetryAttemptToClusterIndex(4).has_value()); // Fourth attempt should also fail
}

// Test name configuration.
TEST_F(CompositeClusterTest, NameConfiguration) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
    name: "custom-name"
)EOF";

  initialize(yaml);
  EXPECT_EQ("custom-name", cluster_->name());
}

// Test honor_route_retry_policy configuration.
TEST_F(CompositeClusterTest, HonorRouteRetryPolicyConfiguration) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
      honor_route_retry_policy: false
)EOF";

  initialize(yaml);
  EXPECT_FALSE(cluster_->honorRouteRetryPolicy());
}

// Test cluster selection method configuration.
TEST_F(CompositeClusterTest, ClusterSelectionMethodConfiguration) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
      cluster_selection_method: DEFAULT
)EOF";

  initialize(yaml);
  EXPECT_EQ(envoy::extensions::clusters::composite::v3::CompositeCluster::RetryConfig::DEFAULT,
            cluster_->retryConfig().cluster_selection_method());
}

// Test error conditions - missing retry_config for RETRY mode.
TEST(CompositeClusterErrorTest, MissingRetryConfig) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
)EOF";

  const auto config = TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(yaml);
  const auto typed_config =
      TestUtility::anyConvert<envoy::extensions::clusters::composite::v3::CompositeCluster>(
          config.cluster_type().typed_config());

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context, nullptr, nullptr,
                                                             false);
  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_unique<Cluster>(config, typed_config, factory_context, creation_status);
  EXPECT_FALSE(creation_status.ok());
  EXPECT_THAT(creation_status.message(),
              testing::HasSubstr("retry_config is required when mode is RETRY"));
}

// Test error conditions - empty sub_clusters.
TEST(CompositeClusterErrorTest, EmptySubClusters) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters: []
    retry_config:
      overflow_behavior: FAIL
)EOF";

  const auto config = TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(yaml);
  const auto typed_config =
      TestUtility::anyConvert<envoy::extensions::clusters::composite::v3::CompositeCluster>(
          config.cluster_type().typed_config());

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context, nullptr, nullptr,
                                                             false);
  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_unique<Cluster>(config, typed_config, factory_context, creation_status);
  EXPECT_FALSE(creation_status.ok());
  EXPECT_THAT(creation_status.message(), testing::HasSubstr("must have at least one sub-cluster"));
}

// Test connection lifetime callbacks.
TEST_F(CompositeClusterTest, ConnectionLifetimeCallbacks) {
  CompositeConnectionLifetimeCallbacks callbacks;

  // Create a simple implementation for testing.
  class TestConnectionLifetimeCallbacks
      : public Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks {
  public:
    void onConnectionOpen(Envoy::Http::ConnectionPool::Instance&, std::vector<uint8_t>&,
                          const Network::Connection&) override {
      open_called = true;
    }
    void onConnectionDraining(Envoy::Http::ConnectionPool::Instance&, std::vector<uint8_t>&,
                              const Network::Connection&) override {
      draining_called = true;
    }
    bool open_called = false;
    bool draining_called = false;
  };

  TestConnectionLifetimeCallbacks test_callback;

  // Add callback.
  callbacks.addCallback(&test_callback);

  // Set up mock objects for callback invocation.
  NiceMock<Envoy::Http::ConnectionPool::MockInstance> mock_pool;
  std::vector<uint8_t> hash_key = {1, 2, 3};
  NiceMock<Network::MockConnection> mock_connection;

  // Trigger callbacks.
  callbacks.onConnectionOpen(mock_pool, hash_key, mock_connection);
  callbacks.onConnectionDraining(mock_pool, hash_key, mock_connection);

  // Verify callbacks were called.
  EXPECT_TRUE(test_callback.open_called);
  EXPECT_TRUE(test_callback.draining_called);

  // Remove callback.
  callbacks.removeCallback(&test_callback);

  // Clear all callbacks.
  callbacks.clearCallbacks();
}

// Test load balancer context functionality.
TEST_F(CompositeClusterTest, LoadBalancerContext) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  // Mock load balancer context with retry attempt information.
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  ON_CALL(mock_context, requestStreamInfo()).WillByDefault(Return(&mock_stream_info));
  ON_CALL(mock_stream_info, attemptCount()).WillByDefault(Return(absl::optional<uint32_t>(2)));

  // Test retry attempt count extraction.
  EXPECT_EQ(2, lb.getRetryAttemptCount(&mock_context));

  // Test null context.
  EXPECT_EQ(1, lb.getRetryAttemptCount(nullptr));
}

// Test factory creation.
TEST_F(CompositeClusterTest, FactoryCreation) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    retry_config:
      overflow_behavior: FAIL
)EOF";

  const auto config = TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(yaml);

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context, nullptr, nullptr,
                                                             false);

  ClusterFactory factory;
  auto result = factory.create(config, factory_context);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(nullptr, result.value().first);
  EXPECT_NE(nullptr, result.value().second);
}

// Test getClusterByIndex with valid and invalid indices.
TEST_F(CompositeClusterTest, GetClusterByIndex) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  // Test out of bounds index.
  EXPECT_EQ(nullptr, lb.getClusterByIndex(5));

  // Test valid index but cluster not found.
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("cluster_0"))
      .WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, lb.getClusterByIndex(0));
}

// Test load balancer context with null stream info.
TEST_F(CompositeClusterTest, LoadBalancerContextNullStreamInfo) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  // Mock context returning null stream info.
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  ON_CALL(mock_context, requestStreamInfo()).WillByDefault(Return(nullptr));

  EXPECT_EQ(1, lb.getRetryAttemptCount(&mock_context));
}

// Test chooseHost delegation to sub-cluster.
TEST_F(CompositeClusterTest, ChooseHostDelegation) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  // Set up mocks.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockLoadBalancer> mock_lb;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto cluster_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(1)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("cluster_0"))
      .WillRepeatedly(Return(&mock_cluster));
  EXPECT_CALL(mock_cluster, loadBalancer()).WillRepeatedly(ReturnRef(mock_lb));
  EXPECT_CALL(mock_cluster, info()).WillRepeatedly(Return(cluster_info));
  std::string cluster_name = "cluster_0";
  EXPECT_CALL(*cluster_info, name()).WillRepeatedly(ReturnRef(cluster_name));
  EXPECT_CALL(mock_lb, chooseHost(_)).WillOnce(Return(Upstream::HostSelectionResponse{mock_host}));

  auto result = lb.chooseHost(&context);
  EXPECT_EQ(mock_host, result.host);
}

// Test peekAnotherHost delegation.
TEST_F(CompositeClusterTest, PeekAnotherHostDelegation) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  // Set up mocks.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockLoadBalancer> mock_lb;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(1)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("cluster_0"))
      .WillRepeatedly(Return(&mock_cluster));
  EXPECT_CALL(mock_cluster, loadBalancer()).WillRepeatedly(ReturnRef(mock_lb));
  EXPECT_CALL(mock_lb, peekAnotherHost(_)).WillOnce(Return(mock_host));

  auto result = lb.peekAnotherHost(&context);
  EXPECT_EQ(mock_host, result);
}

// Test selectExistingConnection delegation.
TEST_F(CompositeClusterTest, SelectExistingConnectionDelegation) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  // Set up mocks.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockLoadBalancer> mock_lb;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  std::vector<uint8_t> hash_key = {1, 2, 3};

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(1)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("cluster_0"))
      .WillRepeatedly(Return(&mock_cluster));
  EXPECT_CALL(mock_cluster, loadBalancer()).WillRepeatedly(ReturnRef(mock_lb));

  absl::optional<Upstream::SelectedPoolAndConnection> selected = absl::nullopt;
  EXPECT_CALL(mock_lb, selectExistingConnection(_, _, _)).WillOnce(Return(selected));

  auto result = lb.selectExistingConnection(&context, *mock_host, hash_key);
  EXPECT_FALSE(result.has_value());
}

// Test lifetime callbacks getter.
TEST_F(CompositeClusterTest, LifetimeCallbacksGetter) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  auto callbacks = lb.lifetimeCallbacks();
  EXPECT_TRUE(callbacks.has_value());
}

// Test cluster update callbacks.
TEST_F(CompositeClusterTest, ClusterUpdateCallbacks) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  // Test onClusterAddOrUpdate - should be no-op.
  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  Upstream::ThreadLocalClusterCommand command = [&mock_cluster]() -> Upstream::ThreadLocalCluster& {
    return mock_cluster;
  };
  lb.onClusterAddOrUpdate("new_cluster", command);

  // Test onClusterRemoval - should just log.
  lb.onClusterRemoval("removed_cluster");
}

// Test thread aware load balancer initialization.
TEST_F(CompositeClusterTest, ThreadAwareLoadBalancerInitialization) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  // Verify thread aware LB initializes successfully.
  EXPECT_NE(nullptr, thread_aware_lb_);
  EXPECT_NE(nullptr, thread_aware_lb_->factory());
  EXPECT_TRUE(thread_aware_lb_->initialize().ok());
}

// Test cluster initialization phase.
TEST_F(CompositeClusterTest, ClusterInitializePhase) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  EXPECT_EQ(Upstream::Cluster::InitializePhase::Primary, cluster_->initializePhase());
}

// Test factory name.
TEST_F(CompositeClusterTest, FactoryName) {
  ClusterFactory factory;
  EXPECT_EQ("envoy.clusters.composite", factory.name());
}

// Test context getters.
TEST_F(CompositeClusterTest, ContextGetters) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  EXPECT_NE(nullptr, &cluster_->context());
  EXPECT_NE(nullptr, &cluster_->runtime());
  EXPECT_NE(nullptr, &cluster_->random());
}

// Test load balancer methods with no valid cluster index.
TEST_F(CompositeClusterTest, LoadBalancerMethodsNoValidIndex) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  // Set up context with attempt that will map to invalid index.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(10)));

  // All methods should return null/empty.
  auto host_result = lb.chooseHost(&context);
  EXPECT_EQ(nullptr, host_result.host);

  auto peek_result = lb.peekAnotherHost(&context);
  EXPECT_EQ(nullptr, peek_result);

  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  std::vector<uint8_t> hash_key;
  auto conn_result = lb.selectExistingConnection(&context, *mock_host, hash_key);
  EXPECT_FALSE(conn_result.has_value());
}

// Test member update callback when stream_info returns no attempt count.
TEST_F(CompositeClusterTest, LoadBalancerContextNoAttemptCount) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.CompositeCluster
    mode: RETRY
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_behavior: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->mode(), cluster_->retryConfig(),
                                  cluster_->honorRouteRetryPolicy());

  // Mock context with stream info that returns no attempt count.
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  ON_CALL(mock_context, requestStreamInfo()).WillByDefault(Return(&mock_stream_info));
  ON_CALL(mock_stream_info, attemptCount()).WillByDefault(Return(absl::nullopt));

  EXPECT_EQ(1, lb.getRetryAttemptCount(&mock_context));
}

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
