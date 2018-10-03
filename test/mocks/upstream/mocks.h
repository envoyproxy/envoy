#pragma once

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/http/async_client.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/upstream.h"

#include "common/common/callback_impl.h"
#include "common/upstream/health_discovery_service.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/cluster_info.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Upstream {

class MockHostSet : public HostSet {
public:
  MockHostSet(uint32_t priority = 0,
              uint32_t overprovisioning_factor = kDefaultOverProvisioningFactor);
  ~MockHostSet();

  void runCallbacks(const HostVector added, const HostVector removed) {
    member_update_cb_helper_.runCallbacks(priority(), added, removed);
  }

  Common::CallbackHandle* addMemberUpdateCb(PrioritySet::MemberUpdateCb callback) {
    return member_update_cb_helper_.add(callback);
  }

  // Upstream::HostSet
  MOCK_CONST_METHOD0(hosts, const HostVector&());
  MOCK_CONST_METHOD0(healthyHosts, const HostVector&());
  MOCK_CONST_METHOD0(hostsPerLocality, const HostsPerLocality&());
  MOCK_CONST_METHOD0(healthyHostsPerLocality, const HostsPerLocality&());
  MOCK_CONST_METHOD0(localityWeights, LocalityWeightsConstSharedPtr());
  MOCK_METHOD0(chooseLocality, absl::optional<uint32_t>());
  MOCK_METHOD8(updateHosts, void(std::shared_ptr<const HostVector> hosts,
                                 std::shared_ptr<const HostVector> healthy_hosts,
                                 HostsPerLocalityConstSharedPtr hosts_per_locality,
                                 HostsPerLocalityConstSharedPtr healthy_hosts_per_locality,
                                 LocalityWeightsConstSharedPtr locality_weights,
                                 const HostVector& hosts_added, const HostVector& hosts_removed,
                                 absl::optional<uint32_t> overprovisioning_factor));
  MOCK_CONST_METHOD0(priority, uint32_t());
  uint32_t overprovisioning_factor() const override { return overprovisioning_factor_; }
  void set_overprovisioning_factor(const uint32_t overprovisioning_factor) {
    overprovisioning_factor_ = overprovisioning_factor;
  }

  HostVector hosts_;
  HostVector healthy_hosts_;
  HostsPerLocalitySharedPtr hosts_per_locality_{new HostsPerLocalityImpl()};
  HostsPerLocalitySharedPtr healthy_hosts_per_locality_{new HostsPerLocalityImpl()};
  LocalityWeightsConstSharedPtr locality_weights_{{}};
  Common::CallbackManager<uint32_t, const HostVector&, const HostVector&> member_update_cb_helper_;
  uint32_t priority_{};
  uint32_t overprovisioning_factor_{};
};

class MockPrioritySet : public PrioritySet {
public:
  MockPrioritySet();
  ~MockPrioritySet();

  HostSet& getHostSet(uint32_t priority);
  void runUpdateCallbacks(uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed);

  MOCK_CONST_METHOD1(addMemberUpdateCb, Common::CallbackHandle*(MemberUpdateCb callback));
  MOCK_CONST_METHOD0(hostSetsPerPriority, const std::vector<HostSetPtr>&());
  MOCK_METHOD0(hostSetsPerPriority, std::vector<HostSetPtr>&());

  MockHostSet* getMockHostSet(uint32_t priority) {
    getHostSet(priority); // Ensure the host set exists.
    return reinterpret_cast<MockHostSet*>(host_sets_[priority].get());
  }

  std::vector<HostSetPtr> host_sets_;
  Common::CallbackManager<uint32_t, const HostVector&, const HostVector&> member_update_cb_helper_;
};

class MockRetryPriority : public RetryPriority {
public:
  MockRetryPriority(const PriorityLoad& priority_load) : priority_load_(priority_load) {}
  ~MockRetryPriority();

  const PriorityLoad& determinePriorityLoad(const PrioritySet&, const PriorityLoad&) {
    return priority_load_;
  }

  MOCK_METHOD1(onHostAttempted, void(HostDescriptionConstSharedPtr));

private:
  const PriorityLoad& priority_load_;
};

class MockRetryPriorityFactory : public RetryPriorityFactory {
public:
  MockRetryPriorityFactory(RetryPrioritySharedPtr retry_priority)
      : retry_priority_(retry_priority) {}
  void createRetryPriority(RetryPriorityFactoryCallbacks& callbacks, const Protobuf::Message&,
                           uint32_t) override {
    callbacks.addRetryPriority(retry_priority_);
  }

  std::string name() const override { return "envoy.mock_retry_priority"; }

private:
  RetryPrioritySharedPtr retry_priority_;
};

class MockCluster : public Cluster {
public:
  MockCluster();
  ~MockCluster();

  // Upstream::Cluster
  MOCK_METHOD0(healthChecker, HealthChecker*());
  MOCK_CONST_METHOD0(info, ClusterInfoConstSharedPtr());
  MOCK_METHOD0(outlierDetector, Outlier::Detector*());
  MOCK_CONST_METHOD0(outlierDetector, const Outlier::Detector*());
  MOCK_METHOD1(initialize, void(std::function<void()> callback));
  MOCK_CONST_METHOD0(initializePhase, InitializePhase());
  MOCK_CONST_METHOD0(sourceAddress, const Network::Address::InstanceConstSharedPtr&());
  MOCK_METHOD0(prioritySet, MockPrioritySet&());
  MOCK_CONST_METHOD0(prioritySet, const PrioritySet&());

  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  std::function<void()> initialize_callback_;
  Network::Address::InstanceConstSharedPtr source_address_;
  NiceMock<MockPrioritySet> priority_set_;
};

class MockLoadBalancerContext : public LoadBalancerContext {
public:
  MockLoadBalancerContext();
  ~MockLoadBalancerContext();

  MOCK_METHOD0(computeHashKey, absl::optional<uint64_t>());
  MOCK_METHOD0(metadataMatchCriteria, Router::MetadataMatchCriteria*());
  MOCK_CONST_METHOD0(downstreamConnection, const Network::Connection*());
  MOCK_CONST_METHOD0(downstreamHeaders, const Http::HeaderMap*());
  MOCK_METHOD2(determinePriorityLoad, const PriorityLoad&(const PrioritySet&, const PriorityLoad&));
  MOCK_METHOD1(shouldSelectAnotherHost, bool(const Host&));
  MOCK_CONST_METHOD0(hostSelectionRetryCount, uint32_t());
};

class MockLoadBalancer : public LoadBalancer {
public:
  MockLoadBalancer();
  ~MockLoadBalancer();

  // Upstream::LoadBalancer
  MOCK_METHOD1(chooseHost, HostConstSharedPtr(LoadBalancerContext* context));

  std::shared_ptr<MockHost> host_{new MockHost()};
};

class MockThreadLocalCluster : public ThreadLocalCluster {
public:
  MockThreadLocalCluster();
  ~MockThreadLocalCluster();

  // Upstream::ThreadLocalCluster
  MOCK_METHOD0(prioritySet, const PrioritySet&());
  MOCK_METHOD0(info, ClusterInfoConstSharedPtr());
  MOCK_METHOD0(loadBalancer, LoadBalancer&());

  NiceMock<MockCluster> cluster_;
  NiceMock<MockLoadBalancer> lb_;
};

class MockClusterManagerFactory : public ClusterManagerFactory {
public:
  MockClusterManagerFactory() {}
  ~MockClusterManagerFactory() {}

  Secret::MockSecretManager& secretManager() override { return secret_manager_; };

  MOCK_METHOD8(clusterManagerFromProto,
               ClusterManagerPtr(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                                 Stats::Store& stats, ThreadLocal::Instance& tls,
                                 Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                                 const LocalInfo::LocalInfo& local_info,
                                 AccessLog::AccessLogManager& log_manager, Server::Admin& admin));

  MOCK_METHOD5(allocateConnPool, Http::ConnectionPool::InstancePtr(
                                     Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                                     ResourcePriority priority, Http::Protocol protocol,
                                     const Network::ConnectionSocket::OptionsSharedPtr& options));

  MOCK_METHOD4(
      allocateTcpConnPool,
      Tcp::ConnectionPool::InstancePtr(Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                                       ResourcePriority priority,
                                       const Network::ConnectionSocket::OptionsSharedPtr& options));

  MOCK_METHOD5(clusterFromProto,
               ClusterSharedPtr(const envoy::api::v2::Cluster& cluster, ClusterManager& cm,
                                Outlier::EventLoggerSharedPtr outlier_event_logger,
                                AccessLog::AccessLogManager& log_manager, bool added_via_api));

  MOCK_METHOD3(createCds,
               CdsApiPtr(const envoy::api::v2::core::ConfigSource& cds_config,
                         const absl::optional<envoy::api::v2::core::ConfigSource>& eds_config,
                         ClusterManager& cm));

private:
  NiceMock<Secret::MockSecretManager> secret_manager_;
};

class MockClusterManager : public ClusterManager {
public:
  explicit MockClusterManager(TimeSource& time_source);
  MockClusterManager();
  ~MockClusterManager();

  Host::CreateConnectionData tcpConnForCluster(const std::string& cluster,
                                               LoadBalancerContext* context) override {
    MockHost::MockCreateConnectionData data = tcpConnForCluster_(cluster, context);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_description_};
  }

  ClusterManagerFactory& clusterManagerFactory() override { return cluster_manager_factory_; }

  // Upstream::ClusterManager
  MOCK_METHOD2(addOrUpdateCluster,
               bool(const envoy::api::v2::Cluster& cluster, const std::string& version_info));
  MOCK_METHOD1(setInitializedCb, void(std::function<void()>));
  MOCK_METHOD0(clusters, ClusterInfoMap());
  MOCK_METHOD1(get, ThreadLocalCluster*(const std::string& cluster));
  MOCK_METHOD4(httpConnPoolForCluster,
               Http::ConnectionPool::Instance*(const std::string& cluster,
                                               ResourcePriority priority, Http::Protocol protocol,
                                               LoadBalancerContext* context));
  MOCK_METHOD3(tcpConnPoolForCluster,
               Tcp::ConnectionPool::Instance*(const std::string& cluster, ResourcePriority priority,
                                              LoadBalancerContext* context));
  MOCK_METHOD2(tcpConnForCluster_,
               MockHost::MockCreateConnectionData(const std::string& cluster,
                                                  LoadBalancerContext* context));
  MOCK_METHOD1(httpAsyncClientForCluster, Http::AsyncClient&(const std::string& cluster));
  MOCK_METHOD1(removeCluster, bool(const std::string& cluster));
  MOCK_METHOD0(shutdown, void());
  MOCK_CONST_METHOD0(bindConfig, const envoy::api::v2::core::BindConfig&());
  MOCK_METHOD0(adsMux, Config::GrpcMux&());
  MOCK_METHOD0(grpcAsyncClientManager, Grpc::AsyncClientManager&());
  MOCK_CONST_METHOD0(versionInfo, const std::string());
  MOCK_CONST_METHOD0(localClusterName, const std::string&());
  MOCK_METHOD1(addThreadLocalClusterUpdateCallbacks,
               std::unique_ptr<ClusterUpdateCallbacksHandle>(ClusterUpdateCallbacks& callbacks));

  NiceMock<Http::ConnectionPool::MockInstance> conn_pool_;
  NiceMock<Http::MockAsyncClient> async_client_;
  NiceMock<Tcp::ConnectionPool::MockInstance> tcp_conn_pool_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  envoy::api::v2::core::BindConfig bind_config_;
  NiceMock<Config::MockGrpcMux> ads_mux_;
  NiceMock<Grpc::MockAsyncClientManager> async_client_manager_;
  std::string local_cluster_name_;
  NiceMock<MockClusterManagerFactory> cluster_manager_factory_;
};

class MockHealthChecker : public HealthChecker {
public:
  MockHealthChecker();
  ~MockHealthChecker();

  MOCK_METHOD1(addHostCheckCompleteCb, void(HostStatusCb callback));
  MOCK_METHOD0(start, void());

  void runCallbacks(Upstream::HostSharedPtr host, HealthTransition changed_state) {
    for (const auto& callback : callbacks_) {
      callback(host, changed_state);
    }
  }

  std::list<HostStatusCb> callbacks_;
};

class MockHealthCheckEventLogger : public HealthCheckEventLogger {
public:
  MOCK_METHOD3(logEjectUnhealthy, void(envoy::data::core::v2alpha::HealthCheckerType,
                                       const HostDescriptionConstSharedPtr&,
                                       envoy::data::core::v2alpha::HealthCheckFailureType));
  MOCK_METHOD3(logAddHealthy, void(envoy::data::core::v2alpha::HealthCheckerType,
                                   const HostDescriptionConstSharedPtr&, bool));
};

class MockCdsApi : public CdsApi {
public:
  MockCdsApi();
  ~MockCdsApi();

  MOCK_METHOD0(initialize, void());
  MOCK_METHOD1(setInitializedCb, void(std::function<void()> callback));
  MOCK_CONST_METHOD0(versionInfo, const std::string());

  std::function<void()> initialized_callback_;
};

class MockClusterUpdateCallbacks : public ClusterUpdateCallbacks {
public:
  MockClusterUpdateCallbacks();
  ~MockClusterUpdateCallbacks();

  MOCK_METHOD1(onClusterAddOrUpdate, void(ThreadLocalCluster& cluster));
  MOCK_METHOD1(onClusterRemoval, void(const std::string& cluster_name));
};

class MockClusterInfoFactory : public ClusterInfoFactory, Logger::Loggable<Logger::Id::upstream> {
public:
  MockClusterInfoFactory();
  ~MockClusterInfoFactory();

  MOCK_METHOD10(createClusterInfo,
                ClusterInfoConstSharedPtr(
                    Runtime::Loader& runtime, const envoy::api::v2::Cluster& cluster,
                    const envoy::api::v2::core::BindConfig& bind_config, Stats::Store& stats,
                    Ssl::ContextManager& ssl_context_manager, bool added_via_api,
                    ClusterManager& cm, const LocalInfo::LocalInfo& local_info,
                    Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random));
};

class MockRetryHostPredicate : public RetryHostPredicate {
public:
  MockRetryHostPredicate();
  ~MockRetryHostPredicate();

  MOCK_METHOD1(shouldSelectAnotherHost, bool(const Host& candidate_host));
  MOCK_METHOD1(onHostAttempted, void(HostDescriptionConstSharedPtr));
};

} // namespace Upstream
} // namespace Envoy
