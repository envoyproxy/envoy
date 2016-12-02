#include "mocks.h"

#include "envoy/upstream/load_balancer.h"

#include "common/upstream/upstream_impl.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Upstream {
namespace Outlier {

MockDetectorHostSink::MockDetectorHostSink() {}
MockDetectorHostSink::~MockDetectorHostSink() {}

MockDetector::MockDetector() {
  ON_CALL(*this, addChangedStateCb(_))
      .WillByDefault(Invoke([this](ChangeStateCb cb) -> void { callbacks_.push_back(cb); }));
}

MockDetector::~MockDetector() {}

} // Outlier

MockHostDescription::MockHostDescription() {
  ON_CALL(*this, url()).WillByDefault(ReturnRef(url_));
  ON_CALL(*this, outlierDetector()).WillByDefault(ReturnRef(outlier_detector_));
}

MockHostDescription::~MockHostDescription() {}

MockHost::MockHost() {}
MockHost::~MockHost() {}

MockCluster::MockCluster()
    : stats_(ClusterImplBase::generateStats(stat_prefix_, stats_store_)),
      resource_manager_(new Upstream::ResourceManagerImpl(runtime_, "fake_key", 1, 1024, 1024, 1)) {
  ON_CALL(*this, connectTimeout()).WillByDefault(Return(std::chrono::milliseconds(1)));
  ON_CALL(*this, hosts()).WillByDefault(ReturnRef(hosts_));
  ON_CALL(*this, healthyHosts()).WillByDefault(ReturnRef(healthy_hosts_));
  ON_CALL(*this, hostsPerZone()).WillByDefault(ReturnRef(hosts_per_zone_));
  ON_CALL(*this, healthyHostsPerZone()).WillByDefault(ReturnRef(healthy_hosts_per_zone_));
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, altStatName()).WillByDefault(ReturnRef(alt_stat_name_));
  ON_CALL(*this, lbType()).WillByDefault(Return(Upstream::LoadBalancerType::RoundRobin));
  ON_CALL(*this, addMemberUpdateCb(_))
      .WillByDefault(Invoke([this](MemberUpdateCb cb) -> void { callbacks_.push_back(cb); }));
  ON_CALL(*this, maxRequestsPerConnection())
      .WillByDefault(ReturnPointee(&max_requests_per_connection_));
  ON_CALL(*this, statPrefix()).WillByDefault(ReturnRef(stat_prefix_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
  ON_CALL(*this, resourceManager(_))
      .WillByDefault(Invoke([this](ResourcePriority)
                                -> Upstream::ResourceManager& { return *resource_manager_; }));
}

MockCluster::~MockCluster() {}

MockClusterManager::MockClusterManager() {
  ON_CALL(*this, httpConnPoolForCluster(_, _)).WillByDefault(Return(&conn_pool_));
  ON_CALL(*this, get(_)).WillByDefault(Return(&cluster_));
}

MockClusterManager::~MockClusterManager() {}

MockHealthChecker::MockHealthChecker() {
  ON_CALL(*this, addHostCheckCompleteCb(_))
      .WillByDefault(Invoke([this](HostStatusCb cb) -> void { callbacks_.push_back(cb); }));
}

MockHealthChecker::~MockHealthChecker() {}

} // Upstream
