#include "test/mocks/upstream/mocks.h"

#include <chrono>
#include <functional>

#include "envoy/upstream/load_balancer.h"

#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;
using testing::_;

namespace Envoy {
namespace Upstream {
namespace Outlier {

MockDetectorHostMonitor::MockDetectorHostMonitor() {}
MockDetectorHostMonitor::~MockDetectorHostMonitor() {}

MockEventLogger::MockEventLogger() {}
MockEventLogger::~MockEventLogger() {}

MockDetector::MockDetector() {
  ON_CALL(*this, addChangedStateCb(_)).WillByDefault(Invoke([this](ChangeStateCb cb) -> void {
    callbacks_.push_back(cb);
  }));
}

MockDetector::~MockDetector() {}

} // namespace Outlier

MockHealthCheckHostMonitor::MockHealthCheckHostMonitor() {}
MockHealthCheckHostMonitor::~MockHealthCheckHostMonitor() {}

MockHostDescription::MockHostDescription()
    : address_(Network::Utility::resolveUrl("tcp://10.0.0.1:443")) {
  ON_CALL(*this, hostname()).WillByDefault(ReturnRef(hostname_));
  ON_CALL(*this, address()).WillByDefault(Return(address_));
  ON_CALL(*this, outlierDetector()).WillByDefault(ReturnRef(outlier_detector_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
  ON_CALL(*this, cluster()).WillByDefault(ReturnRef(cluster_));
  ON_CALL(*this, healthChecker()).WillByDefault(ReturnRef(health_checker_));
}

MockHostDescription::~MockHostDescription() {}

MockHost::MockHost() {
  ON_CALL(*this, cluster()).WillByDefault(ReturnRef(cluster_));
  ON_CALL(*this, outlierDetector()).WillByDefault(ReturnRef(outlier_detector_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
}

MockHost::~MockHost() {}

MockLoadBalancerSubsetInfo::MockLoadBalancerSubsetInfo() {
  ON_CALL(*this, isEnabled()).WillByDefault(Return(false));
  ON_CALL(*this, fallbackPolicy())
      .WillByDefault(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));
  ON_CALL(*this, defaultSubset()).WillByDefault(ReturnRef(ProtobufWkt::Struct::default_instance()));
  ON_CALL(*this, subsetKeys()).WillByDefault(ReturnRef(subset_keys_));
}

MockLoadBalancerSubsetInfo::~MockLoadBalancerSubsetInfo() {}

MockClusterInfo::MockClusterInfo()
    : stats_(ClusterInfoImpl::generateStats(stats_store_)),
      load_report_stats_(ClusterInfoImpl::generateLoadReportStats(load_report_stats_store_)),
      resource_manager_(new Upstream::ResourceManagerImpl(runtime_, "fake_key", 1, 1024, 1024, 1)) {

  ON_CALL(*this, connectTimeout()).WillByDefault(Return(std::chrono::milliseconds(1)));
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, http2Settings()).WillByDefault(ReturnRef(http2_settings_));
  ON_CALL(*this, maxRequestsPerConnection())
      .WillByDefault(ReturnPointee(&max_requests_per_connection_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
  ON_CALL(*this, statsScope()).WillByDefault(ReturnRef(stats_store_));
  ON_CALL(*this, loadReportStats()).WillByDefault(ReturnRef(load_report_stats_));
  ON_CALL(*this, sourceAddress()).WillByDefault(ReturnRef(source_address_));
  ON_CALL(*this, resourceManager(_))
      .WillByDefault(Invoke(
          [this](ResourcePriority) -> Upstream::ResourceManager& { return *resource_manager_; }));
  ON_CALL(*this, lbType()).WillByDefault(ReturnPointee(&lb_type_));
  ON_CALL(*this, sourceAddress()).WillByDefault(ReturnRef(source_address_));
  ON_CALL(*this, lbSubsetInfo()).WillByDefault(ReturnRef(lb_subset_));
}

MockClusterInfo::~MockClusterInfo() {}

MockCluster::MockCluster() {
  ON_CALL(*this, addMemberUpdateCb(_))
      .WillByDefault(Invoke([this](MemberUpdateCb cb) -> Common::CallbackHandle* {
        return member_update_cb_helper_.add(cb);
      }));
  ON_CALL(*this, hosts()).WillByDefault(ReturnRef(hosts_));
  ON_CALL(*this, healthyHosts()).WillByDefault(ReturnRef(healthy_hosts_));
  ON_CALL(*this, hostsPerLocality()).WillByDefault(ReturnRef(hosts_per_locality_));
  ON_CALL(*this, healthyHostsPerLocality()).WillByDefault(ReturnRef(healthy_hosts_per_locality_));
  ON_CALL(*this, info()).WillByDefault(Return(info_));
  ON_CALL(*this, setInitializedCb(_))
      .WillByDefault(Invoke([this](std::function<void()> callback) -> void {
        EXPECT_EQ(nullptr, initialize_callback_);
        initialize_callback_ = callback;
      }));
}

MockCluster::~MockCluster() {}

MockLoadBalancer::MockLoadBalancer() { ON_CALL(*this, chooseHost(_)).WillByDefault(Return(host_)); }

MockLoadBalancer::~MockLoadBalancer() {}

MockThreadLocalCluster::MockThreadLocalCluster() {
  ON_CALL(*this, hostSet()).WillByDefault(ReturnRef(cluster_));
  ON_CALL(*this, info()).WillByDefault(Return(cluster_.info_));
  ON_CALL(*this, loadBalancer()).WillByDefault(ReturnRef(lb_));
}

MockThreadLocalCluster::~MockThreadLocalCluster() {}

MockClusterManager::MockClusterManager() {
  ON_CALL(*this, httpConnPoolForCluster(_, _, _)).WillByDefault(Return(&conn_pool_));
  ON_CALL(*this, httpAsyncClientForCluster(_)).WillByDefault(ReturnRef(async_client_));
  ON_CALL(*this, httpAsyncClientForCluster(_)).WillByDefault((ReturnRef(async_client_)));
  ON_CALL(*this, sourceAddress()).WillByDefault(ReturnRef(source_address_));

  // Matches are LIFO so "" will match first.
  ON_CALL(*this, get(_)).WillByDefault(Return(&thread_local_cluster_));
  ON_CALL(*this, get("")).WillByDefault(Return(nullptr));
}

MockClusterManager::~MockClusterManager() {}

MockHealthChecker::MockHealthChecker() {
  ON_CALL(*this, addHostCheckCompleteCb(_)).WillByDefault(Invoke([this](HostStatusCb cb) -> void {
    callbacks_.push_back(cb);
  }));
}

MockHealthChecker::~MockHealthChecker() {}

MockCdsApi::MockCdsApi() {
  ON_CALL(*this, setInitializedCb(_)).WillByDefault(SaveArg<0>(&initialized_callback_));
}

MockCdsApi::~MockCdsApi() {}

} // namespace Upstream
} // namespace Envoy
