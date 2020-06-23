#pragma once

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
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
#include "test/mocks/upstream/load_balancer_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Upstream {

class MockHostSet : public HostSet {
public:
  MockHostSet(uint32_t priority = 0,
              uint32_t overprovisioning_factor = kDefaultOverProvisioningFactor);
  ~MockHostSet() override;

  void runCallbacks(const HostVector added, const HostVector removed) {
    member_update_cb_helper_.runCallbacks(priority(), added, removed);
  }

  Common::CallbackHandle* addMemberUpdateCb(PrioritySet::PriorityUpdateCb callback) {
    return member_update_cb_helper_.add(callback);
  }

  // Upstream::HostSet
  MOCK_METHOD(const HostVector&, hosts, (), (const));
  MOCK_METHOD(HostVectorConstSharedPtr, hostsPtr, (), (const));
  MOCK_METHOD(const HostVector&, healthyHosts, (), (const));
  MOCK_METHOD(HealthyHostVectorConstSharedPtr, healthyHostsPtr, (), (const));
  MOCK_METHOD(const HostVector&, degradedHosts, (), (const));
  MOCK_METHOD(DegradedHostVectorConstSharedPtr, degradedHostsPtr, (), (const));
  MOCK_METHOD(const HostVector&, excludedHosts, (), (const));
  MOCK_METHOD(ExcludedHostVectorConstSharedPtr, excludedHostsPtr, (), (const));
  MOCK_METHOD(const HostsPerLocality&, hostsPerLocality, (), (const));
  MOCK_METHOD(HostsPerLocalityConstSharedPtr, hostsPerLocalityPtr, (), (const));
  MOCK_METHOD(const HostsPerLocality&, healthyHostsPerLocality, (), (const));
  MOCK_METHOD(HostsPerLocalityConstSharedPtr, healthyHostsPerLocalityPtr, (), (const));
  MOCK_METHOD(const HostsPerLocality&, degradedHostsPerLocality, (), (const));
  MOCK_METHOD(HostsPerLocalityConstSharedPtr, degradedHostsPerLocalityPtr, (), (const));
  MOCK_METHOD(const HostsPerLocality&, excludedHostsPerLocality, (), (const));
  MOCK_METHOD(HostsPerLocalityConstSharedPtr, excludedHostsPerLocalityPtr, (), (const));
  MOCK_METHOD(LocalityWeightsConstSharedPtr, localityWeights, (), (const));
  MOCK_METHOD(absl::optional<uint32_t>, chooseHealthyLocality, ());
  MOCK_METHOD(absl::optional<uint32_t>, chooseDegradedLocality, ());
  MOCK_METHOD(uint32_t, priority, (), (const));
  uint32_t overprovisioningFactor() const override { return overprovisioning_factor_; }
  void setOverprovisioningFactor(const uint32_t overprovisioning_factor) {
    overprovisioning_factor_ = overprovisioning_factor;
  }

  HostVector hosts_;
  HostVector healthy_hosts_;
  HostVector degraded_hosts_;
  HostVector excluded_hosts_;
  HostsPerLocalitySharedPtr hosts_per_locality_{new HostsPerLocalityImpl()};
  HostsPerLocalitySharedPtr healthy_hosts_per_locality_{new HostsPerLocalityImpl()};
  HostsPerLocalitySharedPtr degraded_hosts_per_locality_{new HostsPerLocalityImpl()};
  HostsPerLocalitySharedPtr excluded_hosts_per_locality_{new HostsPerLocalityImpl()};
  LocalityWeightsConstSharedPtr locality_weights_{{}};
  Common::CallbackManager<uint32_t, const HostVector&, const HostVector&> member_update_cb_helper_;
  uint32_t priority_{};
  uint32_t overprovisioning_factor_{};
  bool run_in_panic_mode_ = false;
};

class MockPrioritySet : public PrioritySet {
public:
  MockPrioritySet();
  ~MockPrioritySet() override;

  HostSet& getHostSet(uint32_t priority);
  void runUpdateCallbacks(uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed);

  MOCK_METHOD(Common::CallbackHandle*, addMemberUpdateCb, (MemberUpdateCb callback), (const));
  MOCK_METHOD(Common::CallbackHandle*, addPriorityUpdateCb, (PriorityUpdateCb callback), (const));
  MOCK_METHOD(const std::vector<HostSetPtr>&, hostSetsPerPriority, (), (const));
  MOCK_METHOD(std::vector<HostSetPtr>&, hostSetsPerPriority, ());
  MOCK_METHOD(void, updateHosts,
              (uint32_t priority, UpdateHostsParams&& update_hosts_params,
               LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
               const HostVector& hosts_removed, absl::optional<uint32_t> overprovisioning_factor));
  MOCK_METHOD(void, batchHostUpdate, (BatchUpdateCb&));

  MockHostSet* getMockHostSet(uint32_t priority) {
    getHostSet(priority); // Ensure the host set exists.
    return reinterpret_cast<MockHostSet*>(host_sets_[priority].get());
  }

  std::vector<HostSetPtr> host_sets_;
  Common::CallbackManager<const HostVector&, const HostVector&> member_update_cb_helper_;
  Common::CallbackManager<uint32_t, const HostVector&, const HostVector&>
      priority_update_cb_helper_;
};

class MockRetryPriority : public RetryPriority {
public:
  MockRetryPriority(const HealthyLoad& healthy_priority_load,
                    const DegradedLoad& degraded_priority_load)
      : priority_load_({healthy_priority_load, degraded_priority_load}) {}
  MockRetryPriority(const MockRetryPriority& other) : priority_load_(other.priority_load_) {}
  ~MockRetryPriority() override;

  const HealthyAndDegradedLoad& determinePriorityLoad(const PrioritySet&,
                                                      const HealthyAndDegradedLoad&,
                                                      const PriorityMappingFunc&) override {
    return priority_load_;
  }

  MOCK_METHOD(void, onHostAttempted, (HostDescriptionConstSharedPtr));

private:
  const HealthyAndDegradedLoad priority_load_;
};

class MockRetryPriorityFactory : public RetryPriorityFactory {
public:
  MockRetryPriorityFactory(const MockRetryPriority& retry_priority)
      : retry_priority_(retry_priority) {}
  RetryPrioritySharedPtr createRetryPriority(const Protobuf::Message&,
                                             ProtobufMessage::ValidationVisitor&,
                                             uint32_t) override {
    return std::make_shared<NiceMock<MockRetryPriority>>(retry_priority_);
  }

  std::string name() const override { return "envoy.test_retry_priority"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

private:
  const MockRetryPriority& retry_priority_;
};

class MockCluster : public Cluster {
public:
  MockCluster();
  ~MockCluster() override;

  // Upstream::Cluster
  MOCK_METHOD(HealthChecker*, healthChecker, ());
  MOCK_METHOD(ClusterInfoConstSharedPtr, info, (), (const));
  MOCK_METHOD(Outlier::Detector*, outlierDetector, ());
  MOCK_METHOD(const Outlier::Detector*, outlierDetector, (), (const));
  MOCK_METHOD(void, initialize, (std::function<void()> callback));
  MOCK_METHOD(InitializePhase, initializePhase, (), (const));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, sourceAddress, (), (const));

  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  std::function<void()> initialize_callback_;
  Network::Address::InstanceConstSharedPtr source_address_;
};

// Note that we could template the two implementations below, but to avoid having to define the
// ctor/dtor (which is fairly expensive for mocks) in the header file we duplicate the code instead.

// Use this when interaction with a real PrioritySet is needed, e.g. when update callbacks
// needs to be triggered.
class MockClusterRealPrioritySet : public MockCluster {
public:
  MockClusterRealPrioritySet();
  ~MockClusterRealPrioritySet() override;

  // Upstream::Cluster
  PrioritySetImpl& prioritySet() override { return priority_set_; }
  const PrioritySet& prioritySet() const override { return priority_set_; }

  PrioritySetImpl priority_set_;
};

// Use this for additional convenience methods provided by MockPrioritySet.
class MockClusterMockPrioritySet : public MockCluster {
public:
  MockClusterMockPrioritySet();
  ~MockClusterMockPrioritySet() override;

  // Upstream::Cluster
  MockPrioritySet& prioritySet() override { return priority_set_; }
  const PrioritySet& prioritySet() const override { return priority_set_; }

  NiceMock<MockPrioritySet> priority_set_;
};

class MockLoadBalancer : public LoadBalancer {
public:
  MockLoadBalancer();
  ~MockLoadBalancer() override;

  // Upstream::LoadBalancer
  MOCK_METHOD(HostConstSharedPtr, chooseHost, (LoadBalancerContext * context));

  std::shared_ptr<MockHost> host_{new MockHost()};
};

class MockThreadAwareLoadBalancer : public ThreadAwareLoadBalancer {
public:
  MockThreadAwareLoadBalancer();
  ~MockThreadAwareLoadBalancer() override;

  // Upstream::ThreadAwareLoadBalancer
  MOCK_METHOD(LoadBalancerFactorySharedPtr, factory, ());
  MOCK_METHOD(void, initialize, ());
};

class MockThreadLocalCluster : public ThreadLocalCluster {
public:
  MockThreadLocalCluster();
  ~MockThreadLocalCluster() override;

  // Upstream::ThreadLocalCluster
  MOCK_METHOD(const PrioritySet&, prioritySet, ());
  MOCK_METHOD(ClusterInfoConstSharedPtr, info, ());
  MOCK_METHOD(LoadBalancer&, loadBalancer, ());

  NiceMock<MockClusterMockPrioritySet> cluster_;
  NiceMock<MockLoadBalancer> lb_;
};

class MockClusterManagerFactory : public ClusterManagerFactory {
public:
  MockClusterManagerFactory();
  ~MockClusterManagerFactory() override;

  Secret::MockSecretManager& secretManager() override { return secret_manager_; };

  MOCK_METHOD(ClusterManagerPtr, clusterManagerFromProto,
              (const envoy::config::bootstrap::v3::Bootstrap& bootstrap));

  MOCK_METHOD(Http::ConnectionPool::InstancePtr, allocateConnPool,
              (Event::Dispatcher & dispatcher, HostConstSharedPtr host, ResourcePriority priority,
               Http::Protocol protocol, const Network::ConnectionSocket::OptionsSharedPtr& options,
               const Network::TransportSocketOptionsSharedPtr& transport_socket_options));

  MOCK_METHOD(Tcp::ConnectionPool::InstancePtr, allocateTcpConnPool,
              (Event::Dispatcher & dispatcher, HostConstSharedPtr host, ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               Network::TransportSocketOptionsSharedPtr));

  MOCK_METHOD((std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>), clusterFromProto,
              (const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
               Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api));

  MOCK_METHOD(CdsApiPtr, createCds,
              (const envoy::config::core::v3::ConfigSource& cds_config, ClusterManager& cm));

private:
  NiceMock<Secret::MockSecretManager> secret_manager_;
};

class MockClusterUpdateCallbacksHandle : public ClusterUpdateCallbacksHandle {
public:
  MockClusterUpdateCallbacksHandle();
  ~MockClusterUpdateCallbacksHandle() override;
};

class MockClusterManager : public ClusterManager {
public:
  explicit MockClusterManager(TimeSource& time_source);
  MockClusterManager();
  ~MockClusterManager() override;

  ClusterUpdateCallbacksHandlePtr
  addThreadLocalClusterUpdateCallbacks(ClusterUpdateCallbacks& callbacks) override {
    return ClusterUpdateCallbacksHandlePtr{addThreadLocalClusterUpdateCallbacks_(callbacks)};
  }

  Host::CreateConnectionData tcpConnForCluster(const std::string& cluster,
                                               LoadBalancerContext* context) override {
    MockHost::MockCreateConnectionData data = tcpConnForCluster_(cluster, context);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_description_};
  }

  ClusterManagerFactory& clusterManagerFactory() override { return cluster_manager_factory_; }

  // Upstream::ClusterManager
  MOCK_METHOD(bool, addOrUpdateCluster,
              (const envoy::config::cluster::v3::Cluster& cluster,
               const std::string& version_info));
  MOCK_METHOD(void, setPrimaryClustersInitializedCb, (PrimaryClustersReadyCallback));
  MOCK_METHOD(void, setInitializedCb, (InitializationCompleteCallback));
  MOCK_METHOD(void, initializeSecondaryClusters,
              (const envoy::config::bootstrap::v3::Bootstrap& bootstrap));
  MOCK_METHOD(ClusterInfoMap, clusters, ());
  MOCK_METHOD(const ClusterSet&, primaryClusters, ());
  MOCK_METHOD(ThreadLocalCluster*, get, (absl::string_view cluster));
  MOCK_METHOD(Http::ConnectionPool::Instance*, httpConnPoolForCluster,
              (const std::string& cluster, ResourcePriority priority,
               absl::optional<Http::Protocol> downstream_protocol, LoadBalancerContext* context));
  MOCK_METHOD(Tcp::ConnectionPool::Instance*, tcpConnPoolForCluster,
              (const std::string& cluster, ResourcePriority priority,
               LoadBalancerContext* context));
  MOCK_METHOD(MockHost::MockCreateConnectionData, tcpConnForCluster_,
              (const std::string& cluster, LoadBalancerContext* context));
  MOCK_METHOD(Http::AsyncClient&, httpAsyncClientForCluster, (const std::string& cluster));
  MOCK_METHOD(bool, removeCluster, (const std::string& cluster));
  MOCK_METHOD(void, shutdown, ());
  MOCK_METHOD(const envoy::config::core::v3::BindConfig&, bindConfig, (), (const));
  MOCK_METHOD(Config::GrpcMuxSharedPtr, adsMux, ());
  MOCK_METHOD(Grpc::AsyncClientManager&, grpcAsyncClientManager, ());
  MOCK_METHOD(const std::string, versionInfo, (), (const));
  MOCK_METHOD(const absl::optional<std::string>&, localClusterName, (), (const));
  MOCK_METHOD(ClusterUpdateCallbacksHandle*, addThreadLocalClusterUpdateCallbacks_,
              (ClusterUpdateCallbacks & callbacks));
  MOCK_METHOD(Config::SubscriptionFactory&, subscriptionFactory, ());

  NiceMock<Http::ConnectionPool::MockInstance> conn_pool_;
  NiceMock<Http::MockAsyncClient> async_client_;
  NiceMock<Tcp::ConnectionPool::MockInstance> tcp_conn_pool_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  envoy::config::core::v3::BindConfig bind_config_;
  std::shared_ptr<NiceMock<Config::MockGrpcMux>> ads_mux_;
  NiceMock<Grpc::MockAsyncClientManager> async_client_manager_;
  absl::optional<std::string> local_cluster_name_;
  NiceMock<MockClusterManagerFactory> cluster_manager_factory_;
  NiceMock<Config::MockSubscriptionFactory> subscription_factory_;
};

class MockHealthChecker : public HealthChecker {
public:
  MockHealthChecker();
  ~MockHealthChecker() override;

  MOCK_METHOD(void, addHostCheckCompleteCb, (HostStatusCb callback));
  MOCK_METHOD(void, start, ());

  void runCallbacks(Upstream::HostSharedPtr host, HealthTransition changed_state) {
    for (const auto& callback : callbacks_) {
      callback(host, changed_state);
    }
  }

  std::list<HostStatusCb> callbacks_;
};

class MockHealthCheckEventLogger : public HealthCheckEventLogger {
public:
  MOCK_METHOD(void, logEjectUnhealthy,
              (envoy::data::core::v3::HealthCheckerType, const HostDescriptionConstSharedPtr&,
               envoy::data::core::v3::HealthCheckFailureType));
  MOCK_METHOD(void, logAddHealthy,
              (envoy::data::core::v3::HealthCheckerType, const HostDescriptionConstSharedPtr&,
               bool));
  MOCK_METHOD(void, logUnhealthy,
              (envoy::data::core::v3::HealthCheckerType, const HostDescriptionConstSharedPtr&,
               envoy::data::core::v3::HealthCheckFailureType, bool));
  MOCK_METHOD(void, logDegraded,
              (envoy::data::core::v3::HealthCheckerType, const HostDescriptionConstSharedPtr&));
  MOCK_METHOD(void, logNoLongerDegraded,
              (envoy::data::core::v3::HealthCheckerType, const HostDescriptionConstSharedPtr&));
};

class MockCdsApi : public CdsApi {
public:
  MockCdsApi();
  ~MockCdsApi() override;

  MOCK_METHOD(void, initialize, ());
  MOCK_METHOD(void, setInitializedCb, (std::function<void()> callback));
  MOCK_METHOD(const std::string, versionInfo, (), (const));

  std::function<void()> initialized_callback_;
};

class MockClusterUpdateCallbacks : public ClusterUpdateCallbacks {
public:
  MockClusterUpdateCallbacks();
  ~MockClusterUpdateCallbacks() override;

  MOCK_METHOD(void, onClusterAddOrUpdate, (ThreadLocalCluster & cluster));
  MOCK_METHOD(void, onClusterRemoval, (const std::string& cluster_name));
};

class MockClusterInfoFactory : public ClusterInfoFactory, Logger::Loggable<Logger::Id::upstream> {
public:
  MockClusterInfoFactory();
  ~MockClusterInfoFactory() override;

  MOCK_METHOD(ClusterInfoConstSharedPtr, createClusterInfo, (const CreateClusterInfoParams&));
};

class MockRetryHostPredicate : public RetryHostPredicate {
public:
  MockRetryHostPredicate();
  ~MockRetryHostPredicate() override;

  MOCK_METHOD(bool, shouldSelectAnotherHost, (const Host& candidate_host));
  MOCK_METHOD(void, onHostAttempted, (HostDescriptionConstSharedPtr));
};

class TestRetryHostPredicateFactory : public RetryHostPredicateFactory {
public:
  RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message&, uint32_t) override {
    return std::make_shared<NiceMock<MockRetryHostPredicate>>();
  }

  std::string name() const override { return "envoy.test_host_predicate"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }
};

class MockBasicResourceLimit : public ResourceLimit {
public:
  MockBasicResourceLimit();
  ~MockBasicResourceLimit() override;

  MOCK_METHOD(bool, canCreate, ());
  MOCK_METHOD(void, inc, ());
  MOCK_METHOD(void, dec, ());
  MOCK_METHOD(void, decBy, (uint64_t));
  MOCK_METHOD(uint64_t, max, ());
  MOCK_METHOD(uint64_t, count, (), (const));
};

} // namespace Upstream
} // namespace Envoy
