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

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_info.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Upstream {

class MockCluster : public Cluster {
public:
  MockCluster();
  ~MockCluster();

  void runCallbacks(const std::vector<HostSharedPtr> added,
                    const std::vector<HostSharedPtr> removed) {
    member_update_cb_helper_.runCallbacks(added, removed);
  }

  // Upstream::HostSet
  MOCK_CONST_METHOD1(addMemberUpdateCb, Common::CallbackHandle*(MemberUpdateCb callback));
  MOCK_CONST_METHOD0(hosts, const std::vector<HostSharedPtr>&());
  MOCK_CONST_METHOD0(healthyHosts, const std::vector<HostSharedPtr>&());
  MOCK_CONST_METHOD0(hostsPerLocality, const std::vector<std::vector<HostSharedPtr>>&());
  MOCK_CONST_METHOD0(healthyHostsPerLocality, const std::vector<std::vector<HostSharedPtr>>&());

  // Upstream::Cluster
  MOCK_CONST_METHOD0(info, ClusterInfoConstSharedPtr());
  MOCK_CONST_METHOD0(outlierDetector, const Outlier::Detector*());
  MOCK_METHOD0(initialize, void());
  MOCK_CONST_METHOD0(initializePhase, InitializePhase());
  MOCK_METHOD1(setInitializedCb, void(std::function<void()>));
  MOCK_CONST_METHOD0(sourceAddress, const Network::Address::InstanceConstSharedPtr&());

  std::vector<HostSharedPtr> hosts_;
  std::vector<HostSharedPtr> healthy_hosts_;
  std::vector<std::vector<HostSharedPtr>> hosts_per_locality_;
  std::vector<std::vector<HostSharedPtr>> healthy_hosts_per_locality_;
  Common::CallbackManager<const std::vector<HostSharedPtr>&, const std::vector<HostSharedPtr>&>
      member_update_cb_helper_;
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  std::function<void()> initialize_callback_;
  Network::Address::InstanceConstSharedPtr source_address_;
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
  MOCK_METHOD0(hostSet, const HostSet&());
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
  MOCK_METHOD1(addOrUpdatePrimaryCluster, bool(const envoy::api::v2::Cluster& cluster));
  MOCK_METHOD1(setInitializedCb, void(std::function<void()>));
  MOCK_METHOD0(clusters, ClusterInfoMap());
  MOCK_METHOD1(get, ThreadLocalCluster*(const std::string& cluster));
  MOCK_METHOD3(httpConnPoolForCluster,
               Http::ConnectionPool::Instance*(const std::string& cluster,
                                               ResourcePriority priority,
                                               LoadBalancerContext* context));
  MOCK_METHOD2(tcpConnForCluster_,
               MockHost::MockCreateConnectionData(const std::string& cluster,
                                                  LoadBalancerContext* context));
  MOCK_METHOD1(httpAsyncClientForCluster, Http::AsyncClient&(const std::string& cluster));
  MOCK_METHOD1(removePrimaryCluster, bool(const std::string& cluster));
  MOCK_METHOD0(shutdown, void());
  MOCK_CONST_METHOD0(sourceAddress, const Network::Address::InstanceConstSharedPtr&());
  MOCK_METHOD0(adsMux, Config::GrpcMux&());

  NiceMock<Http::ConnectionPool::MockInstance> conn_pool_;
  NiceMock<Http::MockAsyncClient> async_client_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  Network::Address::InstanceConstSharedPtr source_address_;
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
