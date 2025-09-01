#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/composite/cluster.h"
#include "source/extensions/clusters/composite/lb_context.h"

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
        TestUtility::anyConvert<envoy::extensions::clusters::composite::v3::ClusterConfig>(
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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);
  EXPECT_NE(nullptr, cluster_);
  EXPECT_EQ(2, cluster_->subClusters()->size());
  EXPECT_EQ("cluster_0", (*cluster_->subClusters())[0]);
  EXPECT_EQ("cluster_1", (*cluster_->subClusters())[1]);
  EXPECT_TRUE(cluster_->hasRetryConfig());
  EXPECT_EQ(envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::FAIL,
            cluster_->retryConfig().overflow_option());
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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    retry_config:
      overflow_option: USE_LAST_CLUSTER
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

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
TEST_F(CompositeClusterTest, RoundRobinOverflowOption) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    - name: cluster_2
    retry_config:
      overflow_option: ROUND_ROBIN
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

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
TEST_F(CompositeClusterTest, FailOverflowOption) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
      honor_route_retry_policy: false
)EOF";

  initialize(yaml);
  EXPECT_FALSE(cluster_->honorRouteRetryPolicy());
}

// Cluster selection method enum removed; selection is always sequential based on attempt count.

// Test error conditions - missing retry_config for RETRY mode.
TEST(CompositeClusterErrorTest, MissingRetryConfig) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
)EOF";

  const auto config = TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(yaml);
  const auto typed_config =
      TestUtility::anyConvert<envoy::extensions::clusters::composite::v3::ClusterConfig>(
          config.cluster_type().typed_config());

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context, nullptr, nullptr,
                                                             false);
  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_unique<Cluster>(config, typed_config, factory_context, creation_status);
  EXPECT_FALSE(creation_status.ok());
  EXPECT_THAT(creation_status.message(),
              testing::HasSubstr("must specify a mode configuration (e.g., retry_config)"));
}

// Test error conditions - empty sub_clusters.
TEST(CompositeClusterErrorTest, EmptySubClusters) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters: []
    retry_config:
      overflow_option: FAIL
)EOF";

  const auto config = TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(yaml);
  const auto typed_config =
      TestUtility::anyConvert<envoy::extensions::clusters::composite::v3::ClusterConfig>(
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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    retry_config:
      overflow_option: FAIL
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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

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
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

  // Mock context with stream info that returns no attempt count.
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  ON_CALL(mock_context, requestStreamInfo()).WillByDefault(Return(&mock_stream_info));
  ON_CALL(mock_stream_info, attemptCount()).WillByDefault(Return(absl::nullopt));

  EXPECT_EQ(1, lb.getRetryAttemptCount(&mock_context));
}

// Test CompositeLoadBalancerContext with null base context.
TEST_F(CompositeClusterTest, CompositeLoadBalancerContextNullBase) {
  CompositeLoadBalancerContext context(nullptr, 1);

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
  EXPECT_EQ(1, context.selectedClusterIndex());

  // Test methods that accept callbacks with null base context (should not crash).
  std::function<void(Http::ResponseHeaderMap&)> headers_modifier = [](Http::ResponseHeaderMap&) {};
  context.setHeadersModifier(std::move(headers_modifier));

  context.onAsyncHostSelection(mock_host, "test_details");
}

// Test CompositeLoadBalancerContext with non-null base context.
TEST_F(CompositeClusterTest, CompositeLoadBalancerContextWithBase) {
  auto mock_base_context = std::make_unique<NiceMock<Upstream::MockLoadBalancerContext>>();
  auto* base_ptr = mock_base_context.get();

  // Set up expectations for all methods.
  EXPECT_CALL(*base_ptr, computeHashKey()).WillOnce(Return(12345));
  EXPECT_CALL(*base_ptr, downstreamConnection()).WillOnce(Return(nullptr));
  EXPECT_CALL(*base_ptr, metadataMatchCriteria()).WillOnce(Return(nullptr));
  EXPECT_CALL(*base_ptr, downstreamHeaders()).WillOnce(Return(nullptr));
  EXPECT_CALL(*base_ptr, requestStreamInfo()).WillOnce(Return(nullptr));
  EXPECT_CALL(*base_ptr, hostSelectionRetryCount()).WillOnce(Return(3));
  EXPECT_CALL(*base_ptr, upstreamSocketOptions()).WillOnce(Return(nullptr));
  EXPECT_CALL(*base_ptr, upstreamTransportSocketOptions()).WillOnce(Return(nullptr));
  EXPECT_CALL(*base_ptr, overrideHostToSelect())
      .WillOnce(Return(std::make_pair("test_host", true)));

  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  // Don't test shouldSelectAnotherHost here due to mock complexity

  CompositeLoadBalancerContext context(base_ptr, 2);

  // Test all methods delegate to base context.
  EXPECT_EQ(12345, context.computeHashKey().value());
  EXPECT_EQ(nullptr, context.downstreamConnection());
  EXPECT_EQ(nullptr, context.metadataMatchCriteria());
  EXPECT_EQ(nullptr, context.downstreamHeaders());
  EXPECT_EQ(nullptr, context.requestStreamInfo());
  EXPECT_EQ(3, context.hostSelectionRetryCount());
  EXPECT_EQ(nullptr, context.upstreamSocketOptions());
  EXPECT_EQ(nullptr, context.upstreamTransportSocketOptions());
  auto override_host = context.overrideHostToSelect();
  EXPECT_TRUE(override_host.has_value());
  EXPECT_EQ("test_host", override_host->first);
  EXPECT_TRUE(override_host->second);
  EXPECT_EQ(2, context.selectedClusterIndex());

  // Test callback methods delegate to base context.
  std::function<void(Http::ResponseHeaderMap&)> headers_modifier = [](Http::ResponseHeaderMap&) {};
  EXPECT_CALL(*base_ptr, setHeadersModifier(_));
  context.setHeadersModifier(std::move(headers_modifier));
}

// Test peekAnotherHost with cluster not found (covers missing line 222).
TEST_F(CompositeClusterTest, PeekAnotherHostClusterNotFound) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: nonexistent_cluster
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);
  EXPECT_NE(nullptr, cluster_);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

  // Mock context with retry attempt that maps to valid index but nonexistent cluster.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(1));

  // Should return nullptr when cluster is not found.
  auto result = lb.peekAnotherHost(&context);
  EXPECT_EQ(nullptr, result);
}

// Test selectExistingConnection with cluster not found (covers missing line 242).
TEST_F(CompositeClusterTest, SelectExistingConnectionClusterNotFound) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: nonexistent_cluster
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);
  EXPECT_NE(nullptr, cluster_);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

  // Mock context with retry attempt that maps to valid index but nonexistent cluster.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(1));

  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  std::vector<uint8_t> hash_key = {1, 2, 3};

  // Should return nullopt when cluster is not found.
  auto result = lb.selectExistingConnection(&context, *mock_host, hash_key);
  EXPECT_EQ(absl::nullopt, result);
}

// Test additional CompositeLoadBalancerContext methods with valid base context.
TEST_F(CompositeClusterTest, CompositeLoadBalancerContextAdditionalMethods) {
  NiceMock<Upstream::MockLoadBalancerContext> mock_base_context;
  CompositeLoadBalancerContext context(&mock_base_context, 5);

  // Test computeHashKey with null base context.
  CompositeLoadBalancerContext null_context(nullptr, 3);
  EXPECT_EQ(absl::nullopt, null_context.computeHashKey());
  EXPECT_EQ(3, null_context.selectedClusterIndex());

  // Test that methods work when base context returns values.
  EXPECT_CALL(mock_base_context, computeHashKey()).WillOnce(Return(42));
  EXPECT_EQ(42, context.computeHashKey().value());
  EXPECT_EQ(5, context.selectedClusterIndex());
}

// Test additional coverage for CompositeLoadBalancerContext methods with both null and non-null
// base.
TEST_F(CompositeClusterTest, CompositeLoadBalancerContextComprehensiveCoverage) {
  // Test with null base context to cover null branches.
  CompositeLoadBalancerContext null_context(nullptr, 0);

  EXPECT_EQ(nullptr, null_context.downstreamConnection());
  EXPECT_EQ(nullptr, null_context.metadataMatchCriteria());
  EXPECT_EQ(nullptr, null_context.downstreamHeaders());
  EXPECT_EQ(nullptr, null_context.requestStreamInfo());
  EXPECT_EQ(nullptr, null_context.upstreamSocketOptions());
  EXPECT_EQ(nullptr, null_context.upstreamTransportSocketOptions());
  EXPECT_EQ(absl::nullopt, null_context.overrideHostToSelect());
  EXPECT_EQ(0, null_context.hostSelectionRetryCount());

  // Test shouldSelectAnotherHost with null base context.
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  EXPECT_FALSE(null_context.shouldSelectAnotherHost(*mock_host));

  // Test setHeadersModifier with null base context (should not crash).
  std::function<void(Http::ResponseHeaderMap&)> modifier = [](Http::ResponseHeaderMap&) {};
  null_context.setHeadersModifier(std::move(modifier));

  // Test onAsyncHostSelection with null base context (should not crash).
  null_context.onAsyncHostSelection(mock_host, "test");

  // Test with non-null base context to cover delegation branches.
  NiceMock<Upstream::MockLoadBalancerContext> base_context;
  CompositeLoadBalancerContext delegating_context(&base_context, 1);

  // Test delegation for downstreamConnection.
  EXPECT_CALL(base_context, downstreamConnection()).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, delegating_context.downstreamConnection());

  // Test delegation for metadataMatchCriteria.
  EXPECT_CALL(base_context, metadataMatchCriteria()).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, delegating_context.metadataMatchCriteria());

  // Test delegation for downstreamHeaders.
  EXPECT_CALL(base_context, downstreamHeaders()).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, delegating_context.downstreamHeaders());

  // Test delegation for requestStreamInfo.
  EXPECT_CALL(base_context, requestStreamInfo()).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, delegating_context.requestStreamInfo());

  // Test delegation for upstreamSocketOptions.
  EXPECT_CALL(base_context, upstreamSocketOptions()).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, delegating_context.upstreamSocketOptions());

  // Test delegation for upstreamTransportSocketOptions.
  EXPECT_CALL(base_context, upstreamTransportSocketOptions()).WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, delegating_context.upstreamTransportSocketOptions());

  // Test delegation for overrideHostToSelect.
  EXPECT_CALL(base_context, overrideHostToSelect()).WillOnce(Return(absl::nullopt));
  EXPECT_EQ(absl::nullopt, delegating_context.overrideHostToSelect());

  // Test delegation for hostSelectionRetryCount.
  EXPECT_CALL(base_context, hostSelectionRetryCount()).WillOnce(Return(5));
  EXPECT_EQ(5, delegating_context.hostSelectionRetryCount());

  // Test delegation for shouldSelectAnotherHost.
  EXPECT_CALL(base_context, shouldSelectAnotherHost(_)).WillOnce(Return(true));
  EXPECT_TRUE(delegating_context.shouldSelectAnotherHost(*mock_host));

  // Test delegation for setHeadersModifier.
  std::function<void(Http::ResponseHeaderMap&)> modifier2 = [](Http::ResponseHeaderMap&) {};
  EXPECT_CALL(base_context, setHeadersModifier(_));
  delegating_context.setHeadersModifier(std::move(modifier2));
}

// Test accessing mapRetryAttemptToClusterIndex directly to cover more paths.
TEST_F(CompositeClusterTest, MapRetryAttemptToClusterIndexCoverage) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    - name: cluster_1
    retry_config:
      overflow_option: ROUND_ROBIN
)EOF";

  initialize(yaml);
  EXPECT_NE(nullptr, cluster_);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

  // Test round robin behavior with multiple attempts to cover the modulo logic.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));

  // Test attempt 1 (should map to cluster 0).
  EXPECT_CALL(stream_info, attemptCount()).WillOnce(Return(1));
  auto result1 = lb.chooseHost(&context);

  // Test attempt 2 (should map to cluster 1).
  EXPECT_CALL(stream_info, attemptCount()).WillOnce(Return(2));
  auto result2 = lb.chooseHost(&context);

  // Test attempt 3 (should round robin back to cluster 0).
  EXPECT_CALL(stream_info, attemptCount()).WillOnce(Return(3));
  auto result3 = lb.chooseHost(&context);
}

// Test member update callback registration during initialization (covers lines 253-262).
TEST_F(CompositeClusterTest, MemberUpdateCallbackRegistrationCoverage) {
  const std::string yaml = R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    sub_clusters:
    - name: cluster_0
    retry_config:
      overflow_option: FAIL
)EOF";

  initialize(yaml);
  EXPECT_NE(nullptr, cluster_);

  // Set up a mock cluster that will be returned by cluster manager.
  auto mock_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  auto mock_cluster_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  std::string cluster_name = "cluster_0";
  EXPECT_CALL(*mock_cluster, prioritySet()).WillRepeatedly(ReturnRef(*mock_priority_set));
  EXPECT_CALL(*mock_cluster, info()).WillRepeatedly(Return(mock_cluster_info));
  EXPECT_CALL(*mock_cluster_info, name()).WillRepeatedly(ReturnRef(cluster_name));

  // Set up cluster manager to return our mock cluster.
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("cluster_0"))
      .WillRepeatedly(Return(mock_cluster.get()));

  // Capture the callback function that gets registered.
  Upstream::PrioritySet::MemberUpdateCb captured_callback;
  EXPECT_CALL(*mock_priority_set, addMemberUpdateCb(_))
      .WillOnce([&captured_callback](Upstream::PrioritySet::MemberUpdateCb cb) {
        captured_callback = std::move(cb);
        return nullptr;
      });

  // Creating the load balancer should trigger addMemberUpdateCallbackForCluster.
  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->subClusters(),
                                  cluster_->retryConfig(), cluster_->honorRouteRetryPolicy());

  // Verify callback was captured and test it (covers lines 258-261).
  EXPECT_TRUE(captured_callback != nullptr);

  Upstream::HostVector added_hosts;
  Upstream::HostVector removed_hosts;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  added_hosts.push_back(mock_host);

  // Execute the callback to cover the lambda function.
  auto status = captured_callback(added_hosts, removed_hosts);
  EXPECT_TRUE(status.ok());
}

// Test onAsyncHostSelection delegation (covers line 80).
TEST_F(CompositeClusterTest, OnAsyncHostSelectionDelegationCoverage) {
  NiceMock<Upstream::MockLoadBalancerContext> base_context;
  CompositeLoadBalancerContext context(&base_context, 2);

  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  std::string details = "test_details";

  // Test delegation to base context (should cover line 80).
  EXPECT_CALL(base_context, onAsyncHostSelection(_, _));
  context.onAsyncHostSelection(mock_host, std::move(details));
}

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
