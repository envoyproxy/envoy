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
#include "test/test_common/status_utility.h"
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
    THROW_IF_NOT_OK_REF(initializeWithStatus(yaml_config));
  }

  // As above, but surfaces the construction status instead of throwing, for configs expected to
  // be rejected at load time.
  absl::Status initializeWithStatus(const std::string& yaml_config) {
    cluster_config_ = Upstream::parseClusterFromV3Yaml(yaml_config);
    THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
        cluster_config_.cluster_type().typed_config(),
        ProtobufMessage::getStrictValidationVisitor(), config_));

    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               false);

    absl::Status creation_status = absl::OkStatus();
    cluster_ = std::shared_ptr<Cluster>(
        new Cluster(cluster_config_, config_, factory_context, creation_status));
    return creation_status;
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
  EXPECT_EQ(2, cluster_->clusters().size());
  EXPECT_EQ("primary", cluster_->clusters()[0]);
  EXPECT_EQ("secondary", cluster_->clusters()[1]);
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

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

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
  EXPECT_CALL(stream_info_null, attemptCount()).WillOnce(Return(std::nullopt));
  EXPECT_EQ(0, lb.getAttemptCount(&context_null));
}

// Test mapping of attempt count to cluster name using the configured order.
TEST_F(CompositeClusterTest, ClusterNameForAttemptStaticOrder) {
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

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  // Test invalid attempt 0.
  EXPECT_EQ("", lb.clusterForAttempt(nullptr, 0).name_);

  // Test normal mapping (1-based attempts).
  EXPECT_EQ("primary", lb.clusterForAttempt(nullptr, 1).name_);
  EXPECT_EQ("secondary", lb.clusterForAttempt(nullptr, 2).name_);

  // Test overflow - should fail when attempts exceed available clusters.
  EXPECT_EQ("", lb.clusterForAttempt(nullptr, 3).name_);
  EXPECT_EQ("", lb.clusterForAttempt(nullptr, 10).name_);
}

// Selecting itself would recurse until the worker stack overflows.
TEST_F(CompositeClusterTest, SelfReferenceRejected) {
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
    - name: composite_cluster
)EOF";

  const absl::Status status = initializeWithStatus(yaml);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code());
  EXPECT_THAT(std::string(status.message()), testing::HasSubstr("cannot list itself"));
}

// Each position reports its own index, not the index of the first occurrence of the name.
TEST_F(CompositeClusterTest, DuplicateConfiguredNamesReportTheirOwnIndex) {
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
    - name: primary
)EOF";

  initialize(yaml);
  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  EXPECT_EQ("primary", lb.clusterForAttempt(nullptr, 1).name_);
  EXPECT_EQ(0, lb.clusterForAttempt(nullptr, 1).configured_index_);
  EXPECT_EQ("secondary", lb.clusterForAttempt(nullptr, 2).name_);
  EXPECT_EQ(1, lb.clusterForAttempt(nullptr, 2).configured_index_);
  // Attempt 3 is the second "primary" entry, at index 2 - not index 0.
  EXPECT_EQ("primary", lb.clusterForAttempt(nullptr, 3).name_);
  EXPECT_EQ(2, lb.clusterForAttempt(nullptr, 3).configured_index_);
}

// Test that resolveCluster returns nothing when the named cluster is unknown to the manager.
TEST_F(CompositeClusterTest, ResolveClusterUnknownToClusterManager) {
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

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));

  // Neither configured cluster exists in the cluster manager.
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(1)));
  EXPECT_FALSE(static_cast<bool>(lb.resolveCluster(&context)));

  // Attempts past the end of the configured order resolve to nothing as well.
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(3)));
  EXPECT_FALSE(static_cast<bool>(lb.resolveCluster(&context)));
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

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Upstream::MockHost> host;
  std::vector<uint8_t> hash_key;

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(1)));

  // Test all load balancer methods when no cluster is available.
  auto result = lb.chooseHost(&context);
  EXPECT_EQ(nullptr, result.host);

  EXPECT_EQ(nullptr, lb.peekAnotherHost(&context));
  EXPECT_EQ(std::nullopt, lb.selectExistingConnection(&context, host, hash_key));
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

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

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
  EXPECT_OK(thread_aware_lb.initialize());

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
  EXPECT_EQ(std::nullopt, wrapper.computeHashKey());
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
  EXPECT_EQ(2, cluster_->clusters().size());
  EXPECT_EQ("missing_cluster", cluster_->clusters()[0]);
  EXPECT_EQ("another_missing_cluster", cluster_->clusters()[1]);
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

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

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
                                  cluster_->config_);

  // Set up mocks for successful delegation.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockLoadBalancer> mock_lb;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto cluster_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(1)));
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
                                  cluster_->config_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockLoadBalancer> mock_lb;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(1)));
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

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(3)));

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

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(1)));
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
                                  cluster_->config_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockLoadBalancer> mock_lb;
  NiceMock<Upstream::MockHost> mock_host;

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(1)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("select_target"))
      .WillOnce(Return(&mock_cluster));
  EXPECT_CALL(mock_cluster, loadBalancer()).WillOnce(ReturnRef(mock_lb));

  std::vector<uint8_t> hash_key;
  NiceMock<Http::ConnectionPool::MockInstance> mock_pool;
  NiceMock<Network::MockConnection> mock_connection;
  Upstream::SelectedPoolAndConnection expected_result{mock_pool, mock_connection};
  EXPECT_CALL(mock_lb, selectExistingConnection(_, _, _))
      .WillOnce(Return(std::optional<Upstream::SelectedPoolAndConnection>(expected_result)));

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
  std::ignore = cluster_type->mutable_typed_config()->PackFrom(typed_config);

  auto result = factory.create(cluster_config, factory_context);
  EXPECT_OK(result);
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
  Upstream::LoadBalancerContext::OverrideHost override_host{"override_host", true};
  EXPECT_CALL(mock_context, overrideHostToSelect())
      .WillOnce(Return(OptRef<const Upstream::LoadBalancerContext::OverrideHost>(override_host)));
  OptRef<const Upstream::LoadBalancerContext::OverrideHost> result = wrapper.overrideHostToSelect();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(override_host.host, result->host);
  EXPECT_EQ(override_host.strict, result->strict);

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

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  // Mock context for attempt 1 (should map to cluster index 0).
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(mock_context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(1)));

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

  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(context, requestStreamInfo()).WillRepeatedly(Return(&stream_info));

  // Test attempt 3 which exceeds available clusters (only have 2).
  EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(3)));

  auto result = lb.chooseHost(&context);
  EXPECT_EQ(nullptr, result.host);
}

// Fixture for the per-request order override supplied via request dynamic metadata.
class CompositeClusterOrderMetadataTest : public CompositeClusterTest {
public:
  // Three configured clusters, with the order override key pointing at
  // envoy.clusters.composite:order.
  static constexpr absl::string_view ThreeClustersWithOrderKey = R"EOF(
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
    - name: cluster_2
    order_metadata_key:
      key: envoy.clusters.composite
      path:
      - key: order
)EOF";

  // The same three clusters with no order override configured.
  static constexpr absl::string_view ThreeClustersNoOrderKey = R"EOF(
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
    - name: cluster_2
)EOF";

  void SetUp() override {
    ON_CALL(context_, requestStreamInfo()).WillByDefault(Return(&stream_info_));
  }

  // MockStreamInfo returns its `metadata_` member by reference from dynamicMetadata(), so tests
  // populate that member directly rather than adding an expectation.
  void setOrder(const std::vector<std::string>& order) {
    Protobuf::Value value;
    // Unconditional, so an empty `order` stores an empty ListValue rather than KIND_NOT_SET.
    auto* values = value.mutable_list_value();
    for (const auto& name : order) {
      values->add_values()->set_string_value(name);
    }
    setValue(value);
  }

  void setValue(const Protobuf::Value& value) {
    (*(*stream_info_.metadata_.mutable_filter_metadata())["envoy.clusters.composite"]
          .mutable_fields())["order"] = value;
  }

  // Name selected for `attempt`, which is 1-based.
  absl::string_view nameForAttempt(CompositeClusterLoadBalancer& lb, uint32_t attempt) {
    return lb.clusterForAttempt(&context_, attempt).name_;
  }

  NiceMock<Upstream::MockLoadBalancerContext> context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

// The order override key is absent unless configured, and is parsed when it is.
TEST_F(CompositeClusterOrderMetadataTest, OrderMetadataKeyParsing) {
  initialize(std::string(ThreeClustersNoOrderKey));
  EXPECT_FALSE(cluster_->config_->order_metadata_key_.has_value());

  initialize(std::string(ThreeClustersWithOrderKey));
  ASSERT_TRUE(cluster_->config_->order_metadata_key_.has_value());
  EXPECT_EQ("envoy.clusters.composite", cluster_->config_->order_metadata_key_->key_);
  EXPECT_THAT(cluster_->config_->order_metadata_key_->path_, testing::ElementsAre("order"));
}

// The allowlist is built from the configured clusters, and duplicates map to the first index.
TEST_F(CompositeClusterOrderMetadataTest, AllowlistBuiltFromConfiguredClusters) {
  initialize(std::string(ThreeClustersWithOrderKey));
  EXPECT_EQ(3, cluster_->config_->index_by_name_.size());
  EXPECT_EQ(0, cluster_->config_->index_by_name_.at("cluster_0"));
  EXPECT_EQ(2, cluster_->config_->index_by_name_.at("cluster_2"));
  EXPECT_FALSE(cluster_->config_->index_by_name_.contains("not_configured"));

  initialize(R"EOF(
name: composite_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
    clusters:
    - name: repeated_cluster
    - name: other_cluster
    - name: repeated_cluster
)EOF");
  EXPECT_EQ(3, cluster_->clusters().size());
  EXPECT_EQ(2, cluster_->config_->index_by_name_.size());
  EXPECT_EQ(0, cluster_->config_->index_by_name_.at("repeated_cluster"));
}

// A full reorder replaces the configured order for every attempt.
TEST_F(CompositeClusterOrderMetadataTest, FullReorder) {
  initialize(std::string(ThreeClustersWithOrderKey));
  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  setOrder({"cluster_2", "cluster_0", "cluster_1"});
  EXPECT_EQ("cluster_2", nameForAttempt(lb, 1));
  EXPECT_EQ("cluster_0", nameForAttempt(lb, 2));
  EXPECT_EQ("cluster_1", nameForAttempt(lb, 3));
  EXPECT_EQ("", nameForAttempt(lb, 4));
}

// Attempts past the end of a subset fail rather than falling back to the configured tail.
TEST_F(CompositeClusterOrderMetadataTest, OrderedSubsetOverflows) {
  initialize(std::string(ThreeClustersWithOrderKey));
  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  setOrder({"cluster_2", "cluster_0"});
  EXPECT_EQ("cluster_2", nameForAttempt(lb, 1));
  EXPECT_EQ("cluster_0", nameForAttempt(lb, 2));
  EXPECT_EQ("", nameForAttempt(lb, 3));
}

// Names that are not configured are dropped, and the remaining entries keep their relative order.
TEST_F(CompositeClusterOrderMetadataTest, UnknownNamesDropped) {
  initialize(std::string(ThreeClustersWithOrderKey));
  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  setOrder({"not_configured", "cluster_1", "also_not_configured", "cluster_0"});
  EXPECT_EQ("cluster_1", nameForAttempt(lb, 1));
  EXPECT_EQ("cluster_0", nameForAttempt(lb, 2));
  EXPECT_EQ("", nameForAttempt(lb, 3));
}

// A cluster may legitimately be named more than once, consuming two attempts.
TEST_F(CompositeClusterOrderMetadataTest, DuplicateEntriesConsumeSeparateAttempts) {
  initialize(std::string(ThreeClustersWithOrderKey));
  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  setOrder({"cluster_1", "cluster_1", "cluster_0"});
  EXPECT_EQ("cluster_1", nameForAttempt(lb, 1));
  EXPECT_EQ("cluster_1", nameForAttempt(lb, 2));
  EXPECT_EQ("cluster_0", nameForAttempt(lb, 3));
  EXPECT_EQ("", nameForAttempt(lb, 4));
}

// Non-string entries are skipped exactly like unknown names.
TEST_F(CompositeClusterOrderMetadataTest, NonStringEntriesSkipped) {
  initialize(std::string(ThreeClustersWithOrderKey));
  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  Protobuf::Value value;
  auto* values = value.mutable_list_value();
  values->add_values()->set_number_value(42);
  values->add_values()->set_string_value("cluster_2");
  values->add_values()->set_bool_value(true);
  values->add_values()->mutable_struct_value();
  values->add_values()->set_string_value("cluster_0");
  setValue(value);

  EXPECT_EQ("cluster_2", nameForAttempt(lb, 1));
  EXPECT_EQ("cluster_0", nameForAttempt(lb, 2));
  EXPECT_EQ("", nameForAttempt(lb, 3));
}

// Every shape of unusable metadata falls back to the configured order rather than failing.
TEST_F(CompositeClusterOrderMetadataTest, UnusableMetadataFallsBackToConfiguredOrder) {
  initialize(std::string(ThreeClustersWithOrderKey));
  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  const auto expectConfiguredOrder = [&]() {
    EXPECT_EQ("cluster_0", nameForAttempt(lb, 1));
    EXPECT_EQ("cluster_1", nameForAttempt(lb, 2));
    EXPECT_EQ("cluster_2", nameForAttempt(lb, 3));
    EXPECT_EQ("", nameForAttempt(lb, 4));
  };

  // No metadata at all.
  expectConfiguredOrder();

  // Wrong namespace.
  (*(*stream_info_.metadata_.mutable_filter_metadata())["other.namespace"]
        .mutable_fields())["order"]
      .set_string_value("cluster_2");
  expectConfiguredOrder();

  // Right namespace, wrong key.
  (*(*stream_info_.metadata_.mutable_filter_metadata())["envoy.clusters.composite"]
        .mutable_fields())["other_key"]
      .set_string_value("cluster_2");
  expectConfiguredOrder();

  // Value is a bare string rather than a list.
  Protobuf::Value string_value;
  string_value.set_string_value("cluster_2");
  setValue(string_value);
  expectConfiguredOrder();

  // Value is a struct.
  Protobuf::Value struct_value;
  struct_value.mutable_struct_value();
  setValue(struct_value);
  expectConfiguredOrder();

  // Value is a number.
  Protobuf::Value number_value;
  number_value.set_number_value(7);
  setValue(number_value);
  expectConfiguredOrder();

  // Empty list.
  setOrder({});
  expectConfiguredOrder();

  // A list in which nothing names a configured cluster.
  setOrder({"not_configured", "also_not_configured"});
  expectConfiguredOrder();
}

// Metadata is ignored entirely when the cluster does not configure the key.
TEST_F(CompositeClusterOrderMetadataTest, IgnoredWhenKeyNotConfigured) {
  initialize(std::string(ThreeClustersNoOrderKey));
  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  setOrder({"cluster_2", "cluster_0"});
  EXPECT_EQ("cluster_0", nameForAttempt(lb, 1));
  EXPECT_EQ("cluster_1", nameForAttempt(lb, 2));
}

// A null context or absent stream info falls back to the configured order.
TEST_F(CompositeClusterOrderMetadataTest, IgnoredWithoutRequestStreamInfo) {
  initialize(std::string(ThreeClustersWithOrderKey));
  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  setOrder({"cluster_2", "cluster_0"});

  EXPECT_EQ("cluster_0", lb.clusterForAttempt(nullptr, 1).name_);

  EXPECT_CALL(context_, requestStreamInfo()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ("cluster_0", lb.clusterForAttempt(&context_, 1).name_);
}

// A nested metadata path resolves through intermediate structs.
TEST_F(CompositeClusterOrderMetadataTest, NestedMetadataPath) {
  initialize(R"EOF(
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
    order_metadata_key:
      key: envoy.clusters.composite
      path:
      - key: routing
      - key: order
)EOF");
  CompositeClusterLoadBalancer lb(cluster_->info(), cluster_->cluster_manager_, cluster_->config_);

  Protobuf::Value order;
  order.mutable_list_value()->add_values()->set_string_value("cluster_1");
  (*(*(*stream_info_.metadata_.mutable_filter_metadata())["envoy.clusters.composite"]
          .mutable_fields())["routing"]
        .mutable_struct_value()
        ->mutable_fields())["order"] = order;

  EXPECT_EQ("cluster_1", nameForAttempt(lb, 1));
  EXPECT_EQ("", nameForAttempt(lb, 2));
}

// The reported index is the selected cluster's configured index, not its position in the order.
TEST_F(CompositeClusterOrderMetadataTest, ChooseHostUsesOverrideAndReportsConfiguredIndex) {
  initialize(std::string(ThreeClustersWithOrderKey));
  CompositeClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                  cluster_->config_);

  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockLoadBalancer> mock_lb;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();

  setOrder({"cluster_2", "cluster_0"});
  EXPECT_CALL(context_, requestStreamInfo()).WillRepeatedly(Return(&stream_info_));
  EXPECT_CALL(stream_info_, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(1)));

  // Only the override's first entry is consulted; the configured first cluster is not.
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("cluster_2"))
      .WillRepeatedly(Return(&mock_cluster));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("cluster_0")).Times(0);
  EXPECT_CALL(mock_cluster, loadBalancer()).WillRepeatedly(ReturnRef(mock_lb));

  size_t reported_index = 0;
  EXPECT_CALL(mock_lb, chooseHost(_)).WillOnce([&](Upstream::LoadBalancerContext* ctx) {
    reported_index = static_cast<CompositeLoadBalancerContext*>(ctx)->selectedClusterIndex();
    return Upstream::HostSelectionResponse{mock_host};
  });

  auto result = lb.chooseHost(&context_);
  EXPECT_EQ(mock_host, result.host);
  // cluster_2 is at index 2 of the configured list, not index 0 of the override.
  EXPECT_EQ(2, reported_index);
}

// An attempt past the end of a usable override selects no host and consults no cluster.
TEST_F(CompositeClusterOrderMetadataTest, ChooseHostFailsPastEndOfOverride) {
  initialize(std::string(ThreeClustersWithOrderKey));
  CompositeClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                  cluster_->config_);

  setOrder({"cluster_2"});
  EXPECT_CALL(context_, requestStreamInfo()).WillRepeatedly(Return(&stream_info_));
  EXPECT_CALL(stream_info_, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(2)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster(_)).Times(0);

  EXPECT_EQ(nullptr, lb.chooseHost(&context_).host);
}

// peekAnotherHost and selectExistingConnection honor the override as well.
TEST_F(CompositeClusterOrderMetadataTest, PeekAndSelectExistingConnectionUseOverride) {
  initialize(std::string(ThreeClustersWithOrderKey));
  CompositeClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                  cluster_->config_);

  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockLoadBalancer> mock_lb;
  NiceMock<Upstream::MockHost> host;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  std::vector<uint8_t> hash_key;

  setOrder({"cluster_2", "cluster_0"});
  EXPECT_CALL(context_, requestStreamInfo()).WillRepeatedly(Return(&stream_info_));
  EXPECT_CALL(stream_info_, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(1)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("cluster_2"))
      .WillRepeatedly(Return(&mock_cluster));
  EXPECT_CALL(mock_cluster, loadBalancer()).WillRepeatedly(ReturnRef(mock_lb));

  EXPECT_CALL(mock_lb, peekAnotherHost(_)).WillOnce(Return(mock_host));
  EXPECT_EQ(mock_host, lb.peekAnotherHost(&context_));

  EXPECT_CALL(mock_lb, selectExistingConnection(_, _, _)).WillOnce(Return(std::nullopt));
  EXPECT_EQ(std::nullopt, lb.selectExistingConnection(&context_, host, hash_key));
}

// The load balancer holds no per-request state across successive attempts.
TEST_F(CompositeClusterOrderMetadataTest, OverrideStableAcrossAttempts) {
  initialize(std::string(ThreeClustersWithOrderKey));
  CompositeClusterLoadBalancer lb(cluster_->info(), server_context_.cluster_manager_,
                                  cluster_->config_);

  NiceMock<Upstream::MockThreadLocalCluster> mock_cluster;
  NiceMock<Upstream::MockLoadBalancer> mock_lb;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();

  setOrder({"cluster_2", "cluster_0"});
  EXPECT_CALL(context_, requestStreamInfo()).WillRepeatedly(Return(&stream_info_));
  EXPECT_CALL(mock_cluster, loadBalancer()).WillRepeatedly(ReturnRef(mock_lb));
  // HostSelectionResponse is move-only, so build a fresh one per call rather than using Return().
  EXPECT_CALL(mock_lb, chooseHost(_)).WillRepeatedly([mock_host](Upstream::LoadBalancerContext*) {
    return Upstream::HostSelectionResponse{mock_host};
  });

  EXPECT_CALL(stream_info_, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(1)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("cluster_2"))
      .WillOnce(Return(&mock_cluster));
  EXPECT_EQ(mock_host, lb.chooseHost(&context_).host);

  EXPECT_CALL(stream_info_, attemptCount()).WillRepeatedly(Return(std::optional<uint32_t>(2)));
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("cluster_0"))
      .WillOnce(Return(&mock_cluster));
  EXPECT_EQ(mock_host, lb.chooseHost(&context_).host);
}

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
