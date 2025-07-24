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

    // Create thread-aware load balancer for CLUSTER_PROVIDED policy
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
    clusters:
    - name: cluster_0
    - name: cluster_1
    selection_strategy: SEQUENTIAL
    retry_overflow_option: FAIL
)EOF";

  initialize(yaml);
  EXPECT_NE(nullptr, cluster_);
  EXPECT_EQ(2, cluster_->clusters()->size());
  EXPECT_EQ("cluster_0", (*cluster_->clusters())[0]);
  EXPECT_EQ("cluster_1", (*cluster_->clusters())[1]);
  EXPECT_EQ(envoy::extensions::clusters::composite::v3::ClusterConfig::SEQUENTIAL,
            cluster_->selectionStrategy());
  EXPECT_EQ(envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL,
            cluster_->retryOverflowOption());
}

// Test USE_LAST_CLUSTER overflow behavior.
TEST_F(CompositeClusterTest, UseLastClusterBehavior) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: cluster_0
    - name: cluster_1
    selection_strategy: SEQUENTIAL
    retry_overflow_option: USE_LAST_CLUSTER
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->clusters(),
                                  cluster_->selectionStrategy(), cluster_->retryOverflowOption());

  // Test normal mapping (1-based retry attempts).
  EXPECT_FALSE(lb.mapRetryAttemptToClusterIndex(0).has_value()); // Invalid attempt 0
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(1).value());     // First attempt -> first cluster
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(2).value());     // Second attempt -> second cluster

  // Test overflow with USE_LAST_CLUSTER behavior.
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(3).value());  // Should use last cluster (index 1)
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(10).value()); // Should still use last cluster
}

// Test ROUND_ROBIN overflow behavior.
TEST_F(CompositeClusterTest, RoundRobinOverflowBehavior) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: cluster_0
    - name: cluster_1
    - name: cluster_2
    selection_strategy: SEQUENTIAL
    retry_overflow_option: ROUND_ROBIN
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->clusters(),
                                  cluster_->selectionStrategy(), cluster_->retryOverflowOption());

  // Test normal mapping (1-based retry attempts).
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(1).value()); // First attempt -> cluster 0
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(2).value()); // Second attempt -> cluster 1
  EXPECT_EQ(2, lb.mapRetryAttemptToClusterIndex(3).value()); // Third attempt -> cluster 2

  // Test round-robin overflow.
  EXPECT_EQ(
      0, lb.mapRetryAttemptToClusterIndex(4).value()); // Fourth attempt -> cluster 0 (round-robin)
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(5).value()); // Fifth attempt -> cluster 1
  EXPECT_EQ(2, lb.mapRetryAttemptToClusterIndex(6).value()); // Sixth attempt -> cluster 2
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(7).value()); // Seventh attempt -> cluster 0
}

// Test retry attempt extraction from stream info.
TEST_F(CompositeClusterTest, RetryAttemptExtraction) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: cluster_0
    selection_strategy: SEQUENTIAL
    retry_overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->clusters(),
                                  cluster_->selectionStrategy(), cluster_->retryOverflowOption());

  // Test with null context.
  EXPECT_EQ(1, lb.getRetryAttemptCount(nullptr));

  // Test with context containing stream info.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(3)));

  EXPECT_EQ(3, lb.getRetryAttemptCount(&context));
}

// Test cluster index mapping with FAIL behavior.
TEST_F(CompositeClusterTest, ClusterIndexMappingFail) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: cluster_0
    - name: cluster_1
    selection_strategy: SEQUENTIAL
    retry_overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->clusters(),
                                  cluster_->selectionStrategy(), cluster_->retryOverflowOption());

  // Test normal mapping (1-based retry attempts).
  EXPECT_FALSE(lb.mapRetryAttemptToClusterIndex(0).has_value()); // Invalid attempt 0
  EXPECT_EQ(0, lb.mapRetryAttemptToClusterIndex(1).value());     // First attempt -> first cluster
  EXPECT_EQ(1, lb.mapRetryAttemptToClusterIndex(2).value());     // Second attempt -> second cluster

  // Test overflow with FAIL behavior.
  EXPECT_FALSE(lb.mapRetryAttemptToClusterIndex(3).has_value());  // Out of bounds, should fail
  EXPECT_FALSE(lb.mapRetryAttemptToClusterIndex(10).has_value()); // Far out of bounds, should fail
}

// Test getClusterByIndex method.
TEST_F(CompositeClusterTest, GetClusterByIndex) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: cluster_0
    selection_strategy: SEQUENTIAL
    retry_overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->clusters(),
                                  cluster_->selectionStrategy(), cluster_->retryOverflowOption());

  auto cluster = lb.getClusterByIndex(5);
  EXPECT_EQ(nullptr, cluster);
}

// Test getClusterByIndex when cluster is not found.
TEST_F(CompositeClusterTest, GetClusterByIndexClusterNotFound) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: missing_cluster
    selection_strategy: SEQUENTIAL
    retry_overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->clusters(),
                                  cluster_->selectionStrategy(), cluster_->retryOverflowOption());

  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("missing_cluster"))
      .WillOnce(Return(nullptr));

  auto cluster = lb.getClusterByIndex(0);
  EXPECT_EQ(nullptr, cluster);
}

// Test load balancer methods when no cluster is found.
TEST_F(CompositeClusterTest, LoadBalancerMethodsNoCluster) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: missing_cluster
    selection_strategy: SEQUENTIAL
    retry_overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->clusters(),
                                  cluster_->selectionStrategy(), cluster_->retryOverflowOption());

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(absl::optional<uint32_t>(1)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("missing_cluster"))
      .WillRepeatedly(Return(nullptr));

  // Test chooseHost with missing cluster.
  auto host_result = lb.chooseHost(&context);
  EXPECT_EQ(nullptr, host_result.host);

  // Test peekAnotherHost with missing cluster.
  auto peek_result = lb.peekAnotherHost(&context);
  EXPECT_EQ(nullptr, peek_result);

  // Test selectExistingConnection with missing cluster.
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  std::vector<uint8_t> hash_key;
  auto connection_result = lb.selectExistingConnection(&context, *mock_host, hash_key);
  EXPECT_FALSE(connection_result.has_value());
}

// Test cluster update callbacks.
TEST_F(CompositeClusterTest, ClusterUpdateCallbacks) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: cluster_0
    selection_strategy: SEQUENTIAL
    retry_overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->clusters(),
                                  cluster_->selectionStrategy(), cluster_->retryOverflowOption());

  // Test onClusterAddOrUpdate.
  NiceMock<Upstream::MockThreadLocalCluster> mock_tlc;
  Upstream::ThreadLocalClusterCommand command = [&mock_tlc]() -> Upstream::ThreadLocalCluster& {
    return mock_tlc;
  };
  lb.onClusterAddOrUpdate("new_cluster", command);

  // Test onClusterRemoval.
  lb.onClusterRemoval("removed_cluster");
}

// Test cluster factory name.
TEST_F(CompositeClusterTest, ClusterFactoryName) {
  ClusterFactory factory;
  EXPECT_EQ("envoy.clusters.composite", factory.name());
}

// Test lifetime callbacks method.
TEST_F(CompositeClusterTest, LifetimeCallbacks) {
  const std::string yaml = R"EOF(
name: composite_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: cluster_0
    selection_strategy: SEQUENTIAL
    retry_overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->clusters(),
                                  cluster_->selectionStrategy(), cluster_->retryOverflowOption());

  // Test that lifetimeCallbacks() returns a valid OptRef.
  auto callbacks = lb.lifetimeCallbacks();
  EXPECT_TRUE(callbacks.has_value());
}

// Test the getter methods runtime() and random().
TEST_F(CompositeClusterTest, ClusterGetterMethods) {
  const std::string yaml = R"EOF(
name: getter_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: test_cluster
    selection_strategy: SEQUENTIAL
    retry_overflow_option: FAIL
)EOF";

  initialize(yaml);

  EXPECT_NE(nullptr, &cluster_->runtime());
  EXPECT_NE(nullptr, &cluster_->random());
}

// Test LoadBalancerContext methods.
TEST_F(CompositeClusterTest, LoadBalancerContextMethods) {
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  CompositeLoadBalancerContext wrapper(&mock_context, 2);

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

// Test factory create method coverage.
TEST_F(CompositeClusterTest, FactoryCreateMethodCoverage) {
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
  auto* cluster_entry1 = typed_config.add_clusters();
  cluster_entry1->set_name("factory_cluster_1");
  auto* cluster_entry2 = typed_config.add_clusters();
  cluster_entry2->set_name("factory_cluster_2");
  typed_config.set_selection_strategy(
      envoy::extensions::clusters::composite::v3::ClusterConfig::SEQUENTIAL);
  typed_config.set_retry_overflow_option(
      envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL);
  cluster_type->mutable_typed_config()->PackFrom(typed_config);

  auto result = factory.create(cluster_config, factory_context);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(nullptr, result.value().first);
  EXPECT_NE(nullptr, result.value().second);
}

// Test successful chooseHost delegation.
TEST_F(CompositeClusterTest, ChooseHostSuccessfulDelegation) {
  const std::string yaml = R"EOF(
name: delegation_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: target_cluster
    selection_strategy: SEQUENTIAL
    retry_overflow_option: FAIL
)EOF";

  initialize(yaml);

  CompositeClusterLoadBalancer lb(*cluster_->info(), *cluster_manager_, cluster_->clusters(),
                                  cluster_->selectionStrategy(), cluster_->retryOverflowOption());

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

// Test default configuration values.
TEST_F(CompositeClusterTest, DefaultConfigurationValues) {
  const std::string yaml = R"EOF(
name: default_cluster
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: cluster_0
)EOF";

  initialize(yaml);

  // When no explicit values are set, should use defaults.
  EXPECT_EQ(envoy::extensions::clusters::composite::v3::ClusterConfig::SEQUENTIAL,
            cluster_->selectionStrategy());
  EXPECT_EQ(envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL,
            cluster_->retryOverflowOption());
}

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
