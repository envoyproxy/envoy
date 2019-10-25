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
  MOCK_CONST_METHOD0(hosts, const HostVector&());
  MOCK_CONST_METHOD0(hostsPtr, HostVectorConstSharedPtr());
  MOCK_CONST_METHOD0(healthyHosts, const HostVector&());
  MOCK_CONST_METHOD0(healthyHostsPtr, HealthyHostVectorConstSharedPtr());
  MOCK_CONST_METHOD0(degradedHosts, const HostVector&());
  MOCK_CONST_METHOD0(degradedHostsPtr, DegradedHostVectorConstSharedPtr());
  MOCK_CONST_METHOD0(excludedHosts, const HostVector&());
  MOCK_CONST_METHOD0(excludedHostsPtr, ExcludedHostVectorConstSharedPtr());
  MOCK_CONST_METHOD0(hostsPerLocality, const HostsPerLocality&());
  MOCK_CONST_METHOD0(hostsPerLocalityPtr, HostsPerLocalityConstSharedPtr());
  MOCK_CONST_METHOD0(healthyHostsPerLocality, const HostsPerLocality&());
  MOCK_CONST_METHOD0(healthyHostsPerLocalityPtr, HostsPerLocalityConstSharedPtr());
  MOCK_CONST_METHOD0(degradedHostsPerLocality, const HostsPerLocality&());
  MOCK_CONST_METHOD0(degradedHostsPerLocalityPtr, HostsPerLocalityConstSharedPtr());
  MOCK_CONST_METHOD0(excludedHostsPerLocality, const HostsPerLocality&());
  MOCK_CONST_METHOD0(excludedHostsPerLocalityPtr, HostsPerLocalityConstSharedPtr());
  MOCK_CONST_METHOD0(localityWeights, LocalityWeightsConstSharedPtr());
  MOCK_METHOD0(chooseHealthyLocality, absl::optional<uint32_t>());
  MOCK_METHOD0(chooseDegradedLocality, absl::optional<uint32_t>());
  MOCK_CONST_METHOD0(priority, uint32_t());
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

  MOCK_CONST_METHOD1(addMemberUpdateCb, Common::CallbackHandle*(MemberUpdateCb callback));
  MOCK_CONST_METHOD1(addPriorityUpdateCb, Common::CallbackHandle*(PriorityUpdateCb callback));
  MOCK_CONST_METHOD0(hostSetsPerPriority, const std::vector<HostSetPtr>&());
  MOCK_METHOD0(hostSetsPerPriority, std::vector<HostSetPtr>&());
  MOCK_METHOD6(updateHosts, void(uint32_t priority, UpdateHostsParams&& update_hosts_params,
                                 LocalityWeightsConstSharedPtr locality_weights,
                                 const HostVector& hosts_added, const HostVector& hosts_removed,
                                 absl::optional<uint32_t> overprovisioning_factor));
  MOCK_METHOD1(batchHostUpdate, void(BatchUpdateCb&));

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
                                                      const HealthyAndDegradedLoad&) override {
    return priority_load_;
  }

  MOCK_METHOD1(onHostAttempted, void(HostDescriptionConstSharedPtr));

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
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }

private:
  const MockRetryPriority& retry_priority_;
};

class MockCluster : public Cluster {
public:
  MockCluster();
  ~MockCluster() override;

  // Upstream::Cluster
  MOCK_METHOD0(healthChecker, HealthChecker*());
  MOCK_CONST_METHOD0(info, ClusterInfoConstSharedPtr());
  MOCK_METHOD0(outlierDetector, Outlier::Detector*());
  MOCK_CONST_METHOD0(outlierDetector, const Outlier::Detector*());
  MOCK_METHOD1(initialize, void(std::function<void()> callback));
  MOCK_CONST_METHOD0(initializePhase, InitializePhase());
  MOCK_CONST_METHOD0(sourceAddress, const Network::Address::InstanceConstSharedPtr&());

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
  MOCK_METHOD1(chooseHost, HostConstSharedPtr(LoadBalancerContext* context));

  std::shared_ptr<MockHost> host_{new MockHost()};
};

class MockThreadAwareLoadBalancer : public ThreadAwareLoadBalancer {
public:
  MockThreadAwareLoadBalancer();
  ~MockThreadAwareLoadBalancer() override;

  // Upstream::ThreadAwareLoadBalancer
  MOCK_METHOD0(factory, LoadBalancerFactorySharedPtr());
  MOCK_METHOD0(initialize, void());
};

class MockThreadLocalCluster : public ThreadLocalCluster {
public:
  MockThreadLocalCluster();
  ~MockThreadLocalCluster() override;

  // Upstream::ThreadLocalCluster
  MOCK_METHOD0(prioritySet, const PrioritySet&());
  MOCK_METHOD0(info, ClusterInfoConstSharedPtr());
  MOCK_METHOD0(loadBalancer, LoadBalancer&());

  NiceMock<MockClusterMockPrioritySet> cluster_;
  NiceMock<MockLoadBalancer> lb_;
};

class MockClusterManagerFactory : public ClusterManagerFactory {
public:
  MockClusterManagerFactory();
  ~MockClusterManagerFactory() override;

  Secret::MockSecretManager& secretManager() override { return secret_manager_; };

  MOCK_METHOD1(clusterManagerFromProto,
               ClusterManagerPtr(const envoy::config::bootstrap::v2::Bootstrap& bootstrap));

  MOCK_METHOD6(allocateConnPool,
               Http::ConnectionPool::InstancePtr(
                   Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                   ResourcePriority priority, Http::Protocol protocol,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   const Network::TransportSocketOptionsSharedPtr& transport_socket_options));

  MOCK_METHOD5(allocateTcpConnPool, Tcp::ConnectionPool::InstancePtr(
                                        Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                                        ResourcePriority priority,
                                        const Network::ConnectionSocket::OptionsSharedPtr& options,
                                        Network::TransportSocketOptionsSharedPtr));

  MOCK_METHOD4(clusterFromProto,
               std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>(
                   const envoy::api::v2::Cluster& cluster, ClusterManager& cm,
                   Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api));

  MOCK_METHOD2(createCds,
               CdsApiPtr(const envoy::api::v2::core::ConfigSource& cds_config, ClusterManager& cm));

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
  MOCK_METHOD2(addOrUpdateCluster,
               bool(const envoy::api::v2::Cluster& cluster, const std::string& version_info));
  MOCK_METHOD1(setInitializedCb, void(std::function<void()>));
  MOCK_METHOD0(clusters, ClusterInfoMap());
  MOCK_METHOD1(get, ThreadLocalCluster*(absl::string_view cluster));
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
  MOCK_METHOD0(adsMux, Config::GrpcMuxSharedPtr());
  MOCK_METHOD0(grpcAsyncClientManager, Grpc::AsyncClientManager&());
  MOCK_CONST_METHOD0(versionInfo, const std::string());
  MOCK_CONST_METHOD0(localClusterName, const std::string&());
  MOCK_METHOD1(addThreadLocalClusterUpdateCallbacks_,
               ClusterUpdateCallbacksHandle*(ClusterUpdateCallbacks& callbacks));
  MOCK_CONST_METHOD0(warmingClusterCount, std::size_t());
  MOCK_METHOD0(subscriptionFactory, Config::SubscriptionFactory&());

  NiceMock<Http::ConnectionPool::MockInstance> conn_pool_;
  NiceMock<Http::MockAsyncClient> async_client_;
  NiceMock<Tcp::ConnectionPool::MockInstance> tcp_conn_pool_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  envoy::api::v2::core::BindConfig bind_config_;
  std::shared_ptr<NiceMock<Config::MockGrpcMux>> ads_mux_;
  NiceMock<Grpc::MockAsyncClientManager> async_client_manager_;
  std::string local_cluster_name_;
  NiceMock<MockClusterManagerFactory> cluster_manager_factory_;
  NiceMock<Config::MockSubscriptionFactory> subscription_factory_;
};

class MockHealthChecker : public HealthChecker {
public:
  MockHealthChecker();
  ~MockHealthChecker() override;

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
  MOCK_METHOD4(logUnhealthy, void(envoy::data::core::v2alpha::HealthCheckerType,
                                  const HostDescriptionConstSharedPtr&,
                                  envoy::data::core::v2alpha::HealthCheckFailureType, bool));
  MOCK_METHOD2(logDegraded, void(envoy::data::core::v2alpha::HealthCheckerType,
                                 const HostDescriptionConstSharedPtr&));
  MOCK_METHOD2(logNoLongerDegraded, void(envoy::data::core::v2alpha::HealthCheckerType,
                                         const HostDescriptionConstSharedPtr&));
};

class MockCdsApi : public CdsApi {
public:
  MockCdsApi();
  ~MockCdsApi() override;

  MOCK_METHOD0(initialize, void());
  MOCK_METHOD1(setInitializedCb, void(std::function<void()> callback));
  MOCK_CONST_METHOD0(versionInfo, const std::string());

  std::function<void()> initialized_callback_;
};

class MockClusterUpdateCallbacks : public ClusterUpdateCallbacks {
public:
  MockClusterUpdateCallbacks();
  ~MockClusterUpdateCallbacks() override;

  MOCK_METHOD1(onClusterAddOrUpdate, void(ThreadLocalCluster& cluster));
  MOCK_METHOD1(onClusterRemoval, void(const std::string& cluster_name));
};

class MockClusterInfoFactory : public ClusterInfoFactory, Logger::Loggable<Logger::Id::upstream> {
public:
  MockClusterInfoFactory();
  ~MockClusterInfoFactory() override;

  MOCK_METHOD1(createClusterInfo, ClusterInfoConstSharedPtr(const CreateClusterInfoParams&));
};

class MockRetryHostPredicate : public RetryHostPredicate {
public:
  MockRetryHostPredicate();
  ~MockRetryHostPredicate() override;

  MOCK_METHOD1(shouldSelectAnotherHost, bool(const Host& candidate_host));
  MOCK_METHOD1(onHostAttempted, void(HostDescriptionConstSharedPtr));
};

class TestRetryHostPredicateFactory : public RetryHostPredicateFactory {
public:
  RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message&, uint32_t) override {
    return std::make_shared<NiceMock<MockRetryHostPredicate>>();
  }

  std::string name() override { return "envoy.test_host_predicate"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }
};
} // namespace Upstream
} // namespace Envoy
