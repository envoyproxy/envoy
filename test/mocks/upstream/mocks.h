#pragma once

#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/upstream.h"

#include "common/http/async_client_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"

using testing::NiceMock;

namespace Upstream {

class MockCluster : public Cluster {
public:
  MockCluster();
  ~MockCluster();

  void runCallbacks(const std::vector<HostPtr> added, const std::vector<HostPtr> removed) {
    for (MemberUpdateCb cb : callbacks_) {
      cb(added, removed);
    }
  }

  // Upstream::HostSet
  MOCK_CONST_METHOD1(addMemberUpdateCb, void(MemberUpdateCb callback));
  MOCK_CONST_METHOD0(hosts, const std::vector<HostPtr>&());
  MOCK_CONST_METHOD0(healthyHosts, const std::vector<HostPtr>&());
  MOCK_CONST_METHOD0(hostsPerZone, const std::vector<std::vector<HostPtr>>&());
  MOCK_CONST_METHOD0(healthyHostsPerZone, const std::vector<std::vector<HostPtr>>&());

  // Upstream::Cluster
  MOCK_CONST_METHOD0(altStatName, const std::string&());
  MOCK_CONST_METHOD0(connectTimeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(features, uint64_t());
  MOCK_CONST_METHOD0(httpCodecOptions, uint64_t());
  MOCK_METHOD1(setInitializedCb, void(std::function<void()>));
  MOCK_CONST_METHOD0(sslContext, Ssl::ClientContext*());
  MOCK_CONST_METHOD0(lbType, LoadBalancerType());
  MOCK_CONST_METHOD0(maintenanceMode, bool());
  MOCK_CONST_METHOD0(maxRequestsPerConnection, uint64_t());
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD1(resourceManager, ResourceManager&(ResourcePriority priority));
  MOCK_METHOD0(shutdown, void());
  MOCK_CONST_METHOD0(statPrefix, const std::string&());
  MOCK_CONST_METHOD0(stats, ClusterStats&());

  std::vector<HostPtr> hosts_;
  std::vector<HostPtr> healthy_hosts_;
  std::vector<std::vector<HostPtr>> hosts_per_zone_;
  std::vector<std::vector<HostPtr>> healthy_hosts_per_zone_;
  const std::string name_{"fake_cluster"};
  const std::string alt_stat_name_{"fake_alt_cluster"};
  std::list<MemberUpdateCb> callbacks_;
  uint64_t max_requests_per_connection_{};
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  const std::string stat_prefix_{"cluster.fake_cluster."};
  ClusterStats stats_;
  std::unique_ptr<Upstream::ResourceManager> resource_manager_;
  NiceMock<Runtime::MockLoader> runtime_;
};

class MockClusterManager : public ClusterManager {
public:
  MockClusterManager();
  ~MockClusterManager();

  Host::CreateConnectionData tcpConnForCluster(const std::string& cluster) override {
    MockHost::MockCreateConnectionData data = tcpConnForCluster_(cluster);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_};
  }

  // Upstream::ClusterManager
  MOCK_METHOD1(setInitializedCb, void(std::function<void()>));
  MOCK_METHOD0(clusters, std::unordered_map<std::string, ConstClusterPtr>());
  MOCK_METHOD1(get, const Cluster*(const std::string& cluster));
  MOCK_METHOD2(httpConnPoolForCluster, Http::ConnectionPool::Instance*(const std::string& cluster,
                                                                       ResourcePriority priority));
  MOCK_METHOD1(tcpConnForCluster_, MockHost::MockCreateConnectionData(const std::string& cluster));
  MOCK_METHOD1(httpAsyncClientForCluster, Http::AsyncClient&(const std::string& cluster));
  MOCK_METHOD0(shutdown, void());

  NiceMock<Http::ConnectionPool::MockInstance> conn_pool_;
  NiceMock<MockCluster> cluster_;
  NiceMock<Http::MockAsyncClient> async_client_;
};

class MockHealthChecker : public HealthChecker {
public:
  MockHealthChecker();
  ~MockHealthChecker();

  MOCK_METHOD1(addHostCheckCompleteCb, void(HostStatusCb callback));
  MOCK_METHOD0(start, void());

  void runCallbacks(Upstream::HostPtr host, bool changed_state) {
    for (auto callback : callbacks_) {
      callback(host, changed_state);
    }
  }

  std::list<HostStatusCb> callbacks_;
};

} // Upstream
