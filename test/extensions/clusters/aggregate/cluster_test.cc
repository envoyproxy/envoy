#include "common/singleton/manager_impl.h"

#include "extensions/clusters/aggregate/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"

using testing::AtLeast;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Return;
using testing::ReturnRef;
using testing::SizeIs;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

class AggregateClusterTest : public testing::Test {
public:
  AggregateClusterTest() : stats_(Upstream::ClusterInfoImpl::generateStats(stats_store_)) {}

  void setupPrioritySet() {
    // Set up the HostSet with 1 healthy, 1 degraded and 1 unhealthy.
    Upstream::HostSharedPtr host1 = Upstream::makeTestHost(info_, "tcp://127.0.0.1:80");
    host1->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
    Upstream::HostSharedPtr host2 = Upstream::makeTestHost(info_, "tcp://127.0.0.2:80");
    host2->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
    Upstream::HostSharedPtr host3 = Upstream::makeTestHost(info_, "tcp://127.0.0.3:80");

    // Set up the HostSet with 2 healthy, 2 degraded and 2 unhealthy.
    Upstream::HostSharedPtr host4 = Upstream::makeTestHost(info_, "tcp://127.0.0.4:80");
    Upstream::HostSharedPtr host5 = Upstream::makeTestHost(info_, "tcp://127.0.0.5:80");
    host4->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
    host5->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
    Upstream::HostSharedPtr host6 = Upstream::makeTestHost(info_, "tcp://127.0.0.6:80");
    Upstream::HostSharedPtr host7 = Upstream::makeTestHost(info_, "tcp://127.0.0.7:80");
    host6->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
    host7->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
    Upstream::HostSharedPtr host8 = Upstream::makeTestHost(info_, "tcp://127.0.0.8:80");
    Upstream::HostSharedPtr host9 = Upstream::makeTestHost(info_, "tcp://127.0.0.9:80");

    Upstream::HostVector hosts1{host1, host2, host3},
        hosts2{host4, host5, host6, host7, host8, host9};
    auto hosts1_ptr = std::make_shared<Upstream::HostVector>(hosts1);
    auto hosts2_ptr = std::make_shared<Upstream::HostVector>(hosts2);

    primary_ps_.updateHosts(
        0,
        Upstream::HostSetImpl::partitionHosts(hosts1_ptr, Upstream::HostsPerLocalityImpl::empty()),
        nullptr, hosts1, {}, 100);

    primary_ps_.updateHosts(
        1,
        Upstream::HostSetImpl::partitionHosts(hosts2_ptr, Upstream::HostsPerLocalityImpl::empty()),
        nullptr, hosts2, {}, 100);

    secondary_ps_.updateHosts(
        0,
        Upstream::HostSetImpl::partitionHosts(hosts2_ptr, Upstream::HostsPerLocalityImpl::empty()),
        nullptr, hosts2, {}, 100);

    secondary_ps_.updateHosts(
        1,
        Upstream::HostSetImpl::partitionHosts(hosts1_ptr, Upstream::HostsPerLocalityImpl::empty()),
        nullptr, hosts1, {}, 100);

    ON_CALL(primary_, prioritySet()).WillByDefault(ReturnRef(primary_ps_));
    ON_CALL(secondary_, prioritySet()).WillByDefault(ReturnRef(secondary_ps_));
  }

  void initialize(const std::string& yaml_config) {
    envoy::api::v2::Cluster cluster_config = Upstream::parseClusterFromV2Yaml(yaml_config);
    envoy::config::cluster::aggregate::ClusterConfig config;
    Config::Utility::translateOpaqueConfig(cluster_config.cluster_type().typed_config(),
                                           ProtobufWkt::Struct::default_instance(),
                                           ProtobufMessage::getStrictValidationVisitor(), config);
    Stats::ScopePtr scope = stats_store_.createScope("cluster.name.");
    Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_store_,
        singleton_manager_, tls_, validation_visitor_, *api_);

    cluster_ = std::make_shared<Cluster>(cluster_config, config, runtime_, factory_context,
                                         std::move(scope), false);

    thread_aware_lb_ = std::make_unique<AggregateThreadAwareLoadBalancer>(
        *cluster_, cm_, stats_, runtime_, random_, common_config_);
    lb_factory_ = thread_aware_lb_->factory();

    EXPECT_CALL(cm_, get(Eq("primary"))).WillRepeatedly(Return(&primary_));
    EXPECT_CALL(cm_, get(Eq("secondary"))).WillRepeatedly(Return(&secondary_));
    EXPECT_CALL(cm_, get(Eq("tertiary"))).WillRepeatedly(Return(nullptr));
    setupPrioritySet();
    refreshLb();
  }

  void refreshLb() { lb_ = lb_factory_->create(); }

  envoy::api::v2::Cluster::CommonLbConfig common_config_;
  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_{Api::createApiForTest(stats_store_)};
  std::shared_ptr<Cluster> cluster_;
  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
  Upstream::LoadBalancerFactorySharedPtr lb_factory_;
  Upstream::LoadBalancerPtr lb_;
  Upstream::ClusterStats stats_;
  std::shared_ptr<Upstream::MockClusterInfo> info_{new NiceMock<Upstream::MockClusterInfo>()};
  NiceMock<Upstream::MockThreadLocalCluster> primary_, secondary_;
  Upstream::PrioritySetImpl primary_ps_, secondary_ps_;
  NiceMock<Upstream::MockLoadBalancer> primary_load_balancer_, secondary_load_balancer_;

  const std::string default_yaml_config_ = R"EOF(
    name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.config.cluster.aggregate.ClusterConfig
        clusters:
        - primary
        - secondary
)EOF";
};

TEST_F(AggregateClusterTest, BasicFlow) {
  initialize(default_yaml_config_);
  EXPECT_EQ(cluster_->initializePhase(), Upstream::Cluster::InitializePhase::Secondary);
  auto primary = cm_.get("primary");
  auto secondary = cm_.get("secondary");
  EXPECT_NE(nullptr, primary);
  EXPECT_NE(nullptr, secondary);

  std::pair<Upstream::PrioritySetImpl,
            std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>>>
      pair = ClusterUtil::linearizePrioritySet(cm_, {"primary", "secondary"});

  EXPECT_EQ(pair.first.hostSetsPerPriority().size(), 4);
  EXPECT_EQ(pair.second.size(), 4);

  std::vector<std::vector<int>> expect_cnt{{1, 1, 3, 0}, {2, 2, 6, 1}, {2, 2, 6, 2}, {1, 1, 3, 3}};
  std::vector<Upstream::ThreadLocalCluster*> expect_cluster{primary, primary, secondary, secondary};
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(expect_cnt[i][0], pair.first.hostSetsPerPriority()[i]->healthyHosts().size());
    EXPECT_EQ(expect_cnt[i][1], pair.first.hostSetsPerPriority()[i]->degradedHosts().size());
    EXPECT_EQ(expect_cnt[i][2], pair.first.hostSetsPerPriority()[i]->hosts().size());
    EXPECT_EQ(expect_cnt[i][3], pair.first.hostSetsPerPriority()[i]->priority());
    EXPECT_EQ(expect_cluster[i], pair.second[i].second);
  }
}

TEST_F(AggregateClusterTest, LoadBalancerTest) {
  initialize(default_yaml_config_);
  // Health value:
  // Cluster 1:
  //     Priority 0: 33.3%
  //     Priority 1: 33.3%
  // Cluster 2:
  //     Priority 0: 33.3%
  //     Priority 1: 33.3%
  Upstream::HostSharedPtr host = Upstream::makeTestHost(info_, "tcp://127.0.0.1:80");
  EXPECT_CALL(primary_, loadBalancer()).WillRepeatedly(ReturnRef(primary_load_balancer_));
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));
  EXPECT_CALL(secondary_, loadBalancer()).WillRepeatedly(ReturnRef(secondary_load_balancer_));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));

  for (int i = 0; i <= 65; ++i) {
    EXPECT_CALL(random_, random()).WillOnce(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
    EXPECT_EQ(host.get(), target.get());
  }

  EXPECT_CALL(primary_, loadBalancer()).WillRepeatedly(ReturnRef(primary_load_balancer_));
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(secondary_, loadBalancer()).WillRepeatedly(ReturnRef(secondary_load_balancer_));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));
  for (int i = 66; i < 100; ++i) {
    EXPECT_CALL(random_, random()).WillOnce(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
    EXPECT_EQ(host.get(), target.get());
  }

  // Set up the HostSet with 1 healthy, 1 degraded and 1 unhealthy.
  Upstream::HostSharedPtr host1 = Upstream::makeTestHost(info_, "tcp://127.0.1.1:80");
  host1->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
  Upstream::HostSharedPtr host2 = Upstream::makeTestHost(info_, "tcp://127.0.1.2:80");
  Upstream::HostSharedPtr host3 = Upstream::makeTestHost(info_, "tcp://127.0.1.3:80");
  host2->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
  host3->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
  Upstream::HostSharedPtr host4 = Upstream::makeTestHost(info_, "tcp://127.0.1.4:80");

  Upstream::HostVector hosts{host1, host2, host3, host4};
  auto hosts_ptr = std::make_shared<Upstream::HostVector>(hosts);

  primary_ps_.updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(hosts_ptr, Upstream::HostsPerLocalityImpl::empty()),
      nullptr, hosts, {}, 100);

  refreshLb();
  // Health value:
  // Cluster 1:
  //     Priority 0: 25%
  //     Priority 1: 33.3%
  // Cluster 2:
  //     Priority 0: 33.3%
  //     Priority 1: 33.3%
  EXPECT_CALL(primary_, loadBalancer()).WillRepeatedly(ReturnRef(primary_load_balancer_));
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));
  EXPECT_CALL(secondary_, loadBalancer()).WillRepeatedly(ReturnRef(secondary_load_balancer_));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));

  for (int i = 0; i <= 57; ++i) {
    EXPECT_CALL(random_, random()).WillOnce(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
    EXPECT_EQ(host.get(), target.get());
  }

  EXPECT_CALL(primary_, loadBalancer()).WillRepeatedly(ReturnRef(primary_load_balancer_));
  EXPECT_CALL(primary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(secondary_, loadBalancer()).WillRepeatedly(ReturnRef(secondary_load_balancer_));
  EXPECT_CALL(secondary_load_balancer_, chooseHost(_)).WillRepeatedly(Return(host));
  for (int i = 58; i < 100; ++i) {
    EXPECT_CALL(random_, random()).WillOnce(Return(i));
    Upstream::HostConstSharedPtr target = lb_->chooseHost(nullptr);
    EXPECT_EQ(host.get(), target.get());
  }
}

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
