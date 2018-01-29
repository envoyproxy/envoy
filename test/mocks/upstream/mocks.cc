#include "test/mocks/upstream/mocks.h"

#include <chrono>
#include <functional>

#include "envoy/upstream/load_balancer.h"

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

MockHostSet::MockHostSet(uint32_t priority) : priority_(priority) {
  ON_CALL(*this, priority()).WillByDefault(Return(priority_));
  ON_CALL(*this, hosts()).WillByDefault(ReturnRef(hosts_));
  ON_CALL(*this, healthyHosts()).WillByDefault(ReturnRef(healthy_hosts_));
  ON_CALL(*this, hostsPerLocality()).WillByDefault(ReturnRef(hosts_per_locality_));
  ON_CALL(*this, healthyHostsPerLocality()).WillByDefault(ReturnRef(healthy_hosts_per_locality_));
}

MockPrioritySet::MockPrioritySet() {
  getHostSet(0);
  ON_CALL(*this, hostSetsPerPriority()).WillByDefault(ReturnRef(host_sets_));
  ON_CALL(testing::Const(*this), hostSetsPerPriority()).WillByDefault(ReturnRef(host_sets_));
  ON_CALL(*this, addMemberUpdateCb(_))
      .WillByDefault(Invoke([this](PrioritySet::MemberUpdateCb cb) -> Common::CallbackHandle* {
        return member_update_cb_helper_.add(cb);
      }));
}

HostSet& MockPrioritySet::getHostSet(uint32_t priority) {
  if (host_sets_.size() < priority + 1) {
    for (size_t i = host_sets_.size(); i <= priority; ++i) {
      auto host_set = new NiceMock<MockHostSet>(i);
      host_sets_.push_back(HostSetPtr{host_set});
      host_set->addMemberUpdateCb([this](uint32_t priority,
                                         const std::vector<HostSharedPtr>& hosts_added,
                                         const std::vector<HostSharedPtr>& hosts_removed) {
        runUpdateCallbacks(priority, hosts_added, hosts_removed);
      });
    }
  }
  return *host_sets_[priority];
}
void MockPrioritySet::runUpdateCallbacks(uint32_t priority,
                                         const std::vector<HostSharedPtr>& hosts_added,
                                         const std::vector<HostSharedPtr>& hosts_removed) {
  member_update_cb_helper_.runCallbacks(priority, hosts_added, hosts_removed);
}

MockCluster::MockCluster() {
  ON_CALL(*this, prioritySet()).WillByDefault(ReturnRef(priority_set_));
  ON_CALL(testing::Const(*this), prioritySet()).WillByDefault(ReturnRef(priority_set_));
  ON_CALL(*this, info()).WillByDefault(Return(info_));
  ON_CALL(*this, initialize(_))
      .WillByDefault(Invoke([this](std::function<void()> callback) -> void {
        EXPECT_EQ(nullptr, initialize_callback_);
        initialize_callback_ = callback;
      }));
}

MockCluster::~MockCluster() {}

MockLoadBalancer::MockLoadBalancer() { ON_CALL(*this, chooseHost(_)).WillByDefault(Return(host_)); }

MockLoadBalancer::~MockLoadBalancer() {}

MockThreadLocalCluster::MockThreadLocalCluster() {
  ON_CALL(*this, prioritySet()).WillByDefault(ReturnRef(cluster_.priority_set_));
  ON_CALL(*this, info()).WillByDefault(Return(cluster_.info_));
  ON_CALL(*this, loadBalancer()).WillByDefault(ReturnRef(lb_));
}

MockThreadLocalCluster::~MockThreadLocalCluster() {}

MockClusterManager::MockClusterManager() {
  ON_CALL(*this, httpConnPoolForCluster(_, _, _, _)).WillByDefault(Return(&conn_pool_));
  ON_CALL(*this, httpAsyncClientForCluster(_)).WillByDefault(ReturnRef(async_client_));
  ON_CALL(*this, httpAsyncClientForCluster(_)).WillByDefault((ReturnRef(async_client_)));
  ON_CALL(*this, sourceAddress()).WillByDefault(ReturnRef(source_address_));
  ON_CALL(*this, adsMux()).WillByDefault(ReturnRef(ads_mux_));
  ON_CALL(*this, grpcAsyncClientManager()).WillByDefault(ReturnRef(async_client_manager_));
  ON_CALL(*this, localClusterName()).WillByDefault((ReturnRef(local_cluster_name_)));

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
