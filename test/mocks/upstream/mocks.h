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
#include "common/upstream/upstream_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_info.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Upstream {

class MockHostSet : public HostSet {
public:
  MockHostSet(uint32_t priority = 0);

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
  MOCK_METHOD6(updateHosts, void(std::shared_ptr<const HostVector> hosts,
                                 std::shared_ptr<const HostVector> healthy_hosts,
                                 HostsPerLocalityConstSharedPtr hosts_per_locality,
                                 HostsPerLocalityConstSharedPtr healthy_hosts_per_locality,
                                 const HostVector& hosts_added, const HostVector& hosts_removed));
  MOCK_CONST_METHOD0(priority, uint32_t());

  HostVector hosts_;
  HostVector healthy_hosts_;
  HostsPerLocalitySharedPtr hosts_per_locality_{new HostsPerLocalityImpl()};
  HostsPerLocalitySharedPtr healthy_hosts_per_locality_{new HostsPerLocalityImpl()};
  Common::CallbackManager<uint32_t, const HostVector&, const HostVector&> member_update_cb_helper_;
  uint32_t priority_{};
};

class MockPrioritySet : public PrioritySet {
public:
  MockPrioritySet();
  ~MockPrioritySet() {}

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

class MockClusterManager : public ClusterManager {
public:
  MockClusterManager();
  ~MockClusterManager();

  Host::CreateConnectionData tcpConnForCluster(const std::string& cluster,
                                               LoadBalancerContext* context) override {
    MockHost::MockCreateConnectionData data = tcpConnForCluster_(cluster, context);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_description_};
  }

  // Upstream::ClusterManager
  MOCK_METHOD1(addOrUpdateCluster, bool(const envoy::api::v2::Cluster& cluster));
  MOCK_METHOD1(setInitializedCb, void(std::function<void()>));
  MOCK_METHOD0(clusters, ClusterInfoMap());
  MOCK_METHOD1(get, ThreadLocalCluster*(const std::string& cluster));
  MOCK_METHOD4(httpConnPoolForCluster,
               Http::ConnectionPool::Instance*(const std::string& cluster,
                                               ResourcePriority priority, Http::Protocol protocol,
                                               LoadBalancerContext* context));
  MOCK_METHOD2(tcpConnForCluster_,
               MockHost::MockCreateConnectionData(const std::string& cluster,
                                                  LoadBalancerContext* context));
  MOCK_METHOD1(httpAsyncClientForCluster, Http::AsyncClient&(const std::string& cluster));
  MOCK_METHOD1(removeCluster, bool(const std::string& cluster));
  MOCK_METHOD0(shutdown, void());
  MOCK_CONST_METHOD0(sourceAddress, const Network::Address::InstanceConstSharedPtr&());
  MOCK_METHOD0(adsMux, Config::GrpcMux&());
  MOCK_METHOD0(grpcAsyncClientManager, Grpc::AsyncClientManager&());
  MOCK_CONST_METHOD0(versionInfo, const std::string());
  MOCK_CONST_METHOD0(localClusterName, const std::string&());

  NiceMock<Http::ConnectionPool::MockInstance> conn_pool_;
  NiceMock<Http::MockAsyncClient> async_client_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  Network::Address::InstanceConstSharedPtr source_address_;
  NiceMock<Config::MockGrpcMux> ads_mux_;
  NiceMock<Grpc::MockAsyncClientManager> async_client_manager_;
  std::string local_cluster_name_;
};

class MockHealthChecker : public HealthChecker {
public:
  MockHealthChecker();
  ~MockHealthChecker();

  MOCK_METHOD1(addHostCheckCompleteCb, void(HostStatusCb callback));
  MOCK_METHOD0(start, void());

  void runCallbacks(Upstream::HostSharedPtr host, bool changed_state) {
    for (const auto& callback : callbacks_) {
      callback(host, changed_state);
    }
  }

  std::list<HostStatusCb> callbacks_;
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

} // namespace Upstream
} // namespace Envoy
