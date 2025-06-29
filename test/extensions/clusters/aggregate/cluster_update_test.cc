#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.validate.h"

#include "source/common/router/context_impl.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/cluster_manager_impl.h"
#include "source/extensions/clusters/aggregate/cluster.h"

#include "test/common/upstream/test_cluster_manager.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/config/xds_manager.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_update_callbacks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

envoy::config::bootstrap::v3::Bootstrap parseBootstrapFromV2Yaml(const std::string& yaml) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(yaml, bootstrap);
  return bootstrap;
}

class AggregateClusterUpdateTest : public Event::TestUsingSimulatedTime,
                                   public testing::TestWithParam<bool> {
public:
  AggregateClusterUpdateTest() : ads_mux_(std::make_shared<NiceMock<Config::MockGrpcMux>>()) {}

  void initialize(const std::string& yaml_config) {
    auto bootstrap = parseBootstrapFromV2Yaml(yaml_config);
    const bool use_deferred_cluster = GetParam();
    bootstrap.mutable_cluster_manager()->set_enable_deferred_cluster_creation(use_deferred_cluster);
    // Replace the adsMux to have mocked GrpcMux object that will allow invoking
    // methods when creating the cluster-manager.
    ON_CALL(factory_.server_context_.xds_manager_, adsMux()).WillByDefault(Return(ads_mux_));
    cluster_manager_ = Upstream::TestClusterManagerImpl::createTestClusterManager(
        bootstrap, factory_, factory_.server_context_);
    ON_CALL(factory_.server_context_, clusterManager()).WillByDefault(ReturnRef(*cluster_manager_));
    THROW_IF_NOT_OK(cluster_manager_->initialize(bootstrap));

    ASSERT_TRUE(cluster_manager_->initializeSecondaryClusters(bootstrap).ok());
    EXPECT_EQ(cluster_manager_->activeClusters().size(), 1);
    cluster_ = cluster_manager_->getThreadLocalCluster("aggregate_cluster");
  }

  NiceMock<Upstream::TestClusterManagerFactory> factory_;
  Upstream::ThreadLocalCluster* cluster_;
  std::shared_ptr<NiceMock<Config::MockGrpcMux>> ads_mux_;
  std::unique_ptr<Upstream::TestClusterManagerImpl> cluster_manager_;

  const std::string default_yaml_config_ = R"EOF(
 static_resources:
  clusters:
  - name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate.v3.ClusterConfig
        clusters:
        - primary
        - secondary
  )EOF";
};

INSTANTIATE_TEST_SUITE_P(DeferredClusters, AggregateClusterUpdateTest, testing::Bool());

TEST_P(AggregateClusterUpdateTest, NoHealthyUpstream) {
  initialize(default_yaml_config_);
  EXPECT_EQ(nullptr, cluster_->loadBalancer().chooseHost(nullptr).host);
}

TEST_P(AggregateClusterUpdateTest, BasicFlow) {
  initialize(default_yaml_config_);

  std::unique_ptr<Upstream::MockClusterUpdateCallbacks> callbacks(
      new NiceMock<Upstream::MockClusterUpdateCallbacks>());
  Upstream::ClusterUpdateCallbacksHandlePtr cb =
      cluster_manager_->addThreadLocalClusterUpdateCallbacks(*callbacks);

  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("primary"), ""));
  auto primary = cluster_manager_->getThreadLocalCluster("primary");
  EXPECT_NE(nullptr, primary);
  auto host = cluster_->loadBalancer().chooseHost(nullptr).host;
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());

  EXPECT_TRUE(
      *cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("secondary"), ""));
  auto secondary = cluster_manager_->getThreadLocalCluster("secondary");
  EXPECT_NE(nullptr, secondary);
  host = cluster_->loadBalancer().chooseHost(nullptr).host;
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());

  EXPECT_TRUE(
      *cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("tertiary"), ""));
  auto tertiary = cluster_manager_->getThreadLocalCluster("tertiary");
  EXPECT_NE(nullptr, tertiary);
  host = cluster_->loadBalancer().chooseHost(nullptr).host;
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());

  EXPECT_TRUE(cluster_manager_->removeCluster("primary"));
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("primary"));
  host = cluster_->loadBalancer().chooseHost(nullptr).host;
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("secondary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());
  EXPECT_EQ(3, cluster_manager_->activeClusters().size());

  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("primary"), ""));
  primary = cluster_manager_->getThreadLocalCluster("primary");
  EXPECT_NE(nullptr, primary);
  host = cluster_->loadBalancer().chooseHost(nullptr).host;
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());
}

TEST_P(AggregateClusterUpdateTest, LoadBalancingTest) {
  initialize(default_yaml_config_);
  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("primary"), ""));
  auto primary = cluster_manager_->getThreadLocalCluster("primary");
  EXPECT_NE(nullptr, primary);
  EXPECT_TRUE(
      *cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("secondary"), ""));
  auto secondary = cluster_manager_->getThreadLocalCluster("secondary");
  EXPECT_NE(nullptr, secondary);

  // Set up the HostSet with 1 healthy, 1 degraded and 1 unhealthy.
  Upstream::HostSharedPtr host1 = Upstream::makeTestHost(primary->info(), "tcp://127.0.0.1:80");
  host1->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
  Upstream::HostSharedPtr host2 = Upstream::makeTestHost(primary->info(), "tcp://127.0.0.2:80");
  host2->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
  Upstream::HostSharedPtr host3 = Upstream::makeTestHost(primary->info(), "tcp://127.0.0.3:80");
  Upstream::Cluster& cluster = cluster_manager_->activeClusters().find("primary")->second;
  cluster.prioritySet().updateHosts(
      0,
      Upstream::HostSetImpl::partitionHosts(
          std::make_shared<Upstream::HostVector>(Upstream::HostVector{host1, host2, host3}),
          Upstream::HostsPerLocalityImpl::empty()),
      nullptr, {host1, host2, host3}, {}, 0, absl::nullopt, 100);

  // Set up the HostSet with 1 healthy, 1 degraded and 1 unhealthy.
  Upstream::HostSharedPtr host4 = Upstream::makeTestHost(secondary->info(), "tcp://127.0.0.4:80");
  host4->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
  Upstream::HostSharedPtr host5 = Upstream::makeTestHost(secondary->info(), "tcp://127.0.0.5:80");
  host5->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
  Upstream::HostSharedPtr host6 = Upstream::makeTestHost(secondary->info(), "tcp://127.0.0.6:80");
  Upstream::Cluster& cluster1 = cluster_manager_->activeClusters().find("secondary")->second;
  cluster1.prioritySet().updateHosts(
      0,
      Upstream::HostSetImpl::partitionHosts(
          std::make_shared<Upstream::HostVector>(Upstream::HostVector{host4, host5, host6}),
          Upstream::HostsPerLocalityImpl::empty()),
      nullptr, {host4, host5, host6}, {}, 0, absl::nullopt, 100);

  Upstream::HostConstSharedPtr host;
  for (int i = 0; i < 33; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    EXPECT_EQ(host3, cluster_->loadBalancer().chooseHost(nullptr).host);
  }

  for (int i = 33; i < 66; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    EXPECT_EQ(host6, cluster_->loadBalancer().chooseHost(nullptr).host);
  }

  for (int i = 66; i < 99; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    EXPECT_EQ(host1, cluster_->loadBalancer().chooseHost(nullptr).host);
  }

  for (int i = 99; i < 100; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    EXPECT_EQ(host4, cluster_->loadBalancer().chooseHost(nullptr).host);
  }

  EXPECT_TRUE(cluster_manager_->removeCluster("primary"));
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("primary"));

  // Set up the HostSet with 1 healthy, 1 degraded and 1 unhealthy.
  Upstream::HostSharedPtr host7 = Upstream::makeTestHost(secondary->info(), "tcp://127.0.0.7:80");
  host7->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
  Upstream::HostSharedPtr host8 = Upstream::makeTestHost(secondary->info(), "tcp://127.0.0.8:80");
  host8->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
  Upstream::HostSharedPtr host9 = Upstream::makeTestHost(secondary->info(), "tcp://127.0.0.9:80");
  cluster1.prioritySet().updateHosts(
      1,
      Upstream::HostSetImpl::partitionHosts(
          std::make_shared<Upstream::HostVector>(Upstream::HostVector{host7, host8, host9}),
          Upstream::HostsPerLocalityImpl::empty()),
      nullptr, {host7, host8, host9}, {}, 0, absl::nullopt, 100);

  // Priority set
  //   Priority 0: 1/3 healthy, 1/3 degraded
  //   Priority 1: 1/3 healthy, 1/3 degraded
  for (int i = 0; i < 33; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    host = cluster_->loadBalancer().chooseHost(nullptr).host;
    EXPECT_EQ(host6, cluster_->loadBalancer().chooseHost(nullptr).host);
  }

  for (int i = 33; i < 66; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    host = cluster_->loadBalancer().chooseHost(nullptr).host;
    EXPECT_EQ(host9, cluster_->loadBalancer().chooseHost(nullptr).host);
  }

  for (int i = 66; i < 99; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    EXPECT_EQ(host4, cluster_->loadBalancer().chooseHost(nullptr).host);
  }

  for (int i = 99; i < 100; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    EXPECT_EQ(host7, cluster_->loadBalancer().chooseHost(nullptr).host);
  }
}

TEST_P(AggregateClusterUpdateTest, InitializeAggregateClusterAfterOtherClusters) {
  const std::string config = R"EOF(
 static_resources:
  clusters:
  - name: primary
    connect_timeout: 5s
    type: STATIC
    load_assignment:
      cluster_name: primary
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 80
    lb_policy: ROUND_ROBIN
  - name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.aggregate.v3.ClusterConfig
        clusters:
        - primary
        - secondary
  )EOF";

  auto bootstrap = parseBootstrapFromV2Yaml(config);
  // Replace the adsMux to have mocked GrpcMux object that will allow invoking
  // methods when creating the cluster-manager.
  ON_CALL(factory_.server_context_.xds_manager_, adsMux()).WillByDefault(Return(ads_mux_));
  cluster_manager_ = Upstream::TestClusterManagerImpl::createTestClusterManager(
      bootstrap, factory_, factory_.server_context_);
  ON_CALL(factory_.server_context_, clusterManager()).WillByDefault(ReturnRef(*cluster_manager_));
  THROW_IF_NOT_OK(cluster_manager_->initialize(bootstrap));

  ASSERT_TRUE(cluster_manager_->initializeSecondaryClusters(bootstrap).ok());
  EXPECT_EQ(cluster_manager_->activeClusters().size(), 2);
  cluster_ = cluster_manager_->getThreadLocalCluster("aggregate_cluster");
  auto primary = cluster_manager_->getThreadLocalCluster("primary");
  EXPECT_NE(nullptr, primary);
  auto host = cluster_->loadBalancer().chooseHost(nullptr).host;
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:80", host->address()->asString());

  // Set up the HostSet with 1 healthy, 1 degraded and 1 unhealthy.
  Upstream::HostSharedPtr host1 = Upstream::makeTestHost(primary->info(), "tcp://127.0.0.1:80");
  host1->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
  Upstream::HostSharedPtr host2 = Upstream::makeTestHost(primary->info(), "tcp://127.0.0.2:80");
  host2->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
  Upstream::HostSharedPtr host3 = Upstream::makeTestHost(primary->info(), "tcp://127.0.0.3:80");
  Upstream::Cluster& cluster = cluster_manager_->activeClusters().find("primary")->second;
  cluster.prioritySet().updateHosts(
      0,
      Upstream::HostSetImpl::partitionHosts(
          std::make_shared<Upstream::HostVector>(Upstream::HostVector{host1, host2, host3}),
          Upstream::HostsPerLocalityImpl::empty()),
      nullptr, {host1, host2, host3}, {}, 0, absl::nullopt, 100);

  for (int i = 0; i < 50; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    EXPECT_EQ(host3, cluster_->loadBalancer().chooseHost(nullptr).host);
  }

  for (int i = 50; i < 100; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    EXPECT_EQ(host1, cluster_->loadBalancer().chooseHost(nullptr).host);
  }
}

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
