#include "common/singleton/manager_impl.h"
#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/cluster_manager_impl.h"

#include "extensions/clusters/aggregate/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"

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
namespace {

// The tests in this file are split between testing with real clusters and some with mock clusters.
// By default we setup to call the real cluster creation function. Individual tests can override
// the expectations when needed.
class TestClusterManagerFactory : public Upstream::ClusterManagerFactory {
public:
  TestClusterManagerFactory() : api_(Api::createApiForTest(stats_)) {
    ON_CALL(*this, clusterFromProto_(_, _, _, _))
        .WillByDefault(Invoke(
            [&](const envoy::api::v2::Cluster& cluster, Upstream::ClusterManager& cm,
                Upstream::Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api)
                -> std::pair<Upstream::ClusterSharedPtr, Upstream::ThreadAwareLoadBalancer*> {
              auto result = Upstream::ClusterFactoryImplBase::create(
                  cluster, cm, stats_, tls_, dns_resolver_, ssl_context_manager_, runtime_, random_,
                  dispatcher_, log_manager_, local_info_, admin_, singleton_manager_,
                  outlier_event_logger, added_via_api, validation_visitor_, *api_);
              // Convert from load balancer unique_ptr -> raw pointer -> unique_ptr.
              return std::make_pair(result.first, result.second.release());
            }));
  }

  Http::ConnectionPool::InstancePtr
  allocateConnPool(Event::Dispatcher&, Upstream::HostConstSharedPtr host,
                   Upstream::ResourcePriority, Http::Protocol,
                   const Network::ConnectionSocket::OptionsSharedPtr& options) override {
    return Http::ConnectionPool::InstancePtr{allocateConnPool_(host, options)};
  }

  Tcp::ConnectionPool::InstancePtr
  allocateTcpConnPool(Event::Dispatcher&, Upstream::HostConstSharedPtr host,
                      Upstream::ResourcePriority,
                      const Network::ConnectionSocket::OptionsSharedPtr&,
                      Network::TransportSocketOptionsSharedPtr) override {
    return Tcp::ConnectionPool::InstancePtr{allocateTcpConnPool_(host)};
  }

  std::pair<Upstream::ClusterSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
  clusterFromProto(const envoy::api::v2::Cluster& cluster, Upstream::ClusterManager& cm,
                   Upstream::Outlier::EventLoggerSharedPtr outlier_event_logger,
                   bool added_via_api) override {
    auto result = clusterFromProto_(cluster, cm, outlier_event_logger, added_via_api);
    return std::make_pair(result.first, Upstream::ThreadAwareLoadBalancerPtr(result.second));
  }

  Upstream::CdsApiPtr createCds(const envoy::api::v2::core::ConfigSource&,
                                Upstream::ClusterManager&) override {
    return Upstream::CdsApiPtr{createCds_()};
  }

  Upstream::ClusterManagerPtr
  clusterManagerFromProto(const envoy::config::bootstrap::v2::Bootstrap& bootstrap) override {
    return Upstream::ClusterManagerPtr{clusterManagerFromProto_(bootstrap)};
  }

  Secret::SecretManager& secretManager() override { return secret_manager_; }

  MOCK_METHOD1(clusterManagerFromProto_,
               Upstream::ClusterManager*(const envoy::config::bootstrap::v2::Bootstrap& bootstrap));
  MOCK_METHOD2(allocateConnPool_,
               Http::ConnectionPool::Instance*(Upstream::HostConstSharedPtr host,
                                               Network::ConnectionSocket::OptionsSharedPtr));
  MOCK_METHOD1(allocateTcpConnPool_,
               Tcp::ConnectionPool::Instance*(Upstream::HostConstSharedPtr host));
  MOCK_METHOD4(clusterFromProto_,
               std::pair<Upstream::ClusterSharedPtr, Upstream::ThreadAwareLoadBalancer*>(
                   const envoy::api::v2::Cluster& cluster, Upstream::ClusterManager& cm,
                   Upstream::Outlier::EventLoggerSharedPtr outlier_event_logger,
                   bool added_via_api));
  MOCK_METHOD0(createCds_, Upstream::CdsApi*());

  Stats::IsolatedStoreImpl stats_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new NiceMock<Network::MockDnsResolver>};
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Extensions::TransportSockets::Tls::ContextManagerImpl ssl_context_manager_{
      dispatcher_.timeSource()};
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  NiceMock<Secret::MockSecretManager> secret_manager_;
  NiceMock<AccessLog::MockAccessLogManager> log_manager_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
};

// A test version of ClusterManagerImpl that provides a way to get a non-const handle to the
// clusters, which is necessary in order to call updateHosts on the priority set.
class TestClusterManagerImpl : public Upstream::ClusterManagerImpl {
public:
  using Upstream::ClusterManagerImpl::ClusterManagerImpl;

  TestClusterManagerImpl(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                         Upstream::ClusterManagerFactory& factory, Stats::Store& stats,
                         ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                         Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                         AccessLog::AccessLogManager& log_manager,
                         Event::Dispatcher& main_thread_dispatcher, Server::Admin& admin,
                         ProtobufMessage::ValidationContext& validation_context, Api::Api& api,
                         Http::Context& http_context)
      : ClusterManagerImpl(bootstrap, factory, stats, tls, runtime, random, local_info, log_manager,
                           main_thread_dispatcher, admin, validation_context, api, http_context) {}

  std::map<std::string, std::reference_wrapper<Upstream::Cluster>> activeClusters() {
    std::map<std::string, std::reference_wrapper<Upstream::Cluster>> clusters;
    for (auto& cluster : active_clusters_) {
      clusters.emplace(cluster.first, *cluster.second->cluster_);
    }
    return clusters;
  }
};

envoy::config::bootstrap::v2::Bootstrap parseBootstrapFromV2Yaml(const std::string& yaml) {
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  TestUtility::loadFromYaml(yaml, bootstrap);
  return bootstrap;
}

class AggregateClusterTest : public testing::Test {
public:
  AggregateClusterTest() : http_context_(stats_store_.symbolTable()) {}

  void initialize(const std::string& yaml_config) {
    cluster_manager_ = std::make_unique<TestClusterManagerImpl>(
        parseBootstrapFromV2Yaml(yaml_config), factory_, factory_.stats_, factory_.tls_,
        factory_.runtime_, factory_.random_, factory_.local_info_, log_manager_,
        factory_.dispatcher_, admin_, validation_context_, *api_, http_context_);

    EXPECT_EQ(cluster_manager_->activeClusters().size(), 1);
    cluster_ = cluster_manager_->get("aggregate_cluster");
  }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Server::MockAdmin> admin_;
  Api::ApiPtr api_{Api::createApiForTest(stats_store_)};
  Upstream::ThreadLocalCluster* cluster_;

  Event::SimulatedTimeSystem time_system_;
  NiceMock<TestClusterManagerFactory> factory_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  std::unique_ptr<TestClusterManagerImpl> cluster_manager_;
  AccessLog::MockAccessLogManager log_manager_;
  Http::ContextImpl http_context_;

  const std::string default_yaml_config_ = R"EOF(
 static_resources:
  clusters:
  - name: aggregate_cluster
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

TEST_F(AggregateClusterTest, NoHealthyUpstream) {
  initialize(default_yaml_config_);
  EXPECT_EQ(nullptr, cluster_->loadBalancer().chooseHost(nullptr));
}

TEST_F(AggregateClusterTest, BasicFlow) {
  initialize(default_yaml_config_);

  std::unique_ptr<Upstream::MockClusterUpdateCallbacks> callbacks(
      new NiceMock<Upstream::MockClusterUpdateCallbacks>());
  Upstream::ClusterUpdateCallbacksHandlePtr cb =
      cluster_manager_->addThreadLocalClusterUpdateCallbacks(*callbacks);

  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("primary"), ""));
  auto primary = cluster_manager_->get("primary");
  EXPECT_NE(nullptr, primary);
  auto host = cluster_->loadBalancer().chooseHost(nullptr);
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());

  EXPECT_TRUE(
      cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("secondary"), ""));
  auto secondary = cluster_manager_->get("secondary");
  EXPECT_NE(nullptr, secondary);
  host = cluster_->loadBalancer().chooseHost(nullptr);
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());

  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("tertiary"), ""));
  auto tertiary = cluster_manager_->get("tertiary");
  EXPECT_NE(nullptr, tertiary);
  host = cluster_->loadBalancer().chooseHost(nullptr);
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());

  EXPECT_TRUE(cluster_manager_->removeCluster("primary"));
  EXPECT_EQ(nullptr, cluster_manager_->get("primary"));
  host = cluster_->loadBalancer().chooseHost(nullptr);
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("secondary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());
  EXPECT_EQ(3, cluster_manager_->activeClusters().size());

  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("primary"), ""));
  primary = cluster_manager_->get("primary");
  EXPECT_NE(nullptr, primary);
  host = cluster_->loadBalancer().chooseHost(nullptr);
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());

  // Set up the HostSet with 1 healthy, 1 degraded and 1 unhealthy.
  Upstream::HostSharedPtr host1 = Upstream::makeTestHost(primary->info(), "tcp://127.0.0.1:80");
  host1->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
  Upstream::HostSharedPtr host2 = Upstream::makeTestHost(primary->info(), "tcp://127.0.0.2:80");
  host2->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
  Upstream::HostSharedPtr host3 = Upstream::makeTestHost(primary->info(), "tcp://127.0.0.3:80");
  Upstream::HostVector hosts{host1, host2, host3};
  auto hosts_ptr = std::make_shared<Upstream::HostVector>(hosts);

  Upstream::Cluster& cluster = cluster_manager_->activeClusters().find("primary")->second;
  cluster.prioritySet().updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(hosts_ptr, Upstream::HostsPerLocalityImpl::empty()),
      nullptr, hosts, {}, 100);

  // Set up the HostSet with 1 healthy, 1 degraded and 1 unhealthy.
  Upstream::HostSharedPtr host4 = Upstream::makeTestHost(secondary->info(), "tcp://127.0.0.4:80");
  host4->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
  Upstream::HostSharedPtr host5 = Upstream::makeTestHost(secondary->info(), "tcp://127.0.0.5:80");
  host5->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
  Upstream::HostSharedPtr host6 = Upstream::makeTestHost(secondary->info(), "tcp://127.0.0.6:80");
  Upstream::HostVector hosts1{host4, host5, host6};
  auto hosts_ptr1 = std::make_shared<Upstream::HostVector>(hosts1);
  Upstream::Cluster& cluster1 = cluster_manager_->activeClusters().find("secondary")->second;
  cluster1.prioritySet().updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(hosts_ptr1, Upstream::HostsPerLocalityImpl::empty()),
      nullptr, hosts1, {}, 100);

  for (int i = 0; i < 33; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    host = cluster_->loadBalancer().chooseHost(nullptr);
    EXPECT_EQ("primary", host->cluster().name());
    EXPECT_EQ(Upstream::Host::Health::Healthy, host->health());
  }

  for (int i = 33; i < 66; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    host = cluster_->loadBalancer().chooseHost(nullptr);
    EXPECT_EQ("secondary", host->cluster().name());
    EXPECT_EQ(Upstream::Host::Health::Healthy, host->health());
  }

  for (int i = 66; i < 99; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    host = cluster_->loadBalancer().chooseHost(nullptr);
    EXPECT_EQ("primary", host->cluster().name());
    EXPECT_EQ(Upstream::Host::Health::Degraded, host->health());
  }

  for (int i = 99; i < 100; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    host = cluster_->loadBalancer().chooseHost(nullptr);
    EXPECT_EQ("secondary", host->cluster().name());
    EXPECT_EQ(Upstream::Host::Health::Degraded, host->health());
  }
}

} // namespace

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy