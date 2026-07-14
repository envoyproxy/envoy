#include "priority_set.h"

#include <chrono>
#include <functional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

using ::testing::_;
using ::testing::Invoke;
using ::testing::ReturnPointee;
using ::testing::ReturnRef;

MockPrioritySet::MockPrioritySet() {
  getHostSet(0);
  ON_CALL(*this, hostSetsPerPriority()).WillByDefault(ReturnRef(host_sets_));
  ON_CALL(testing::Const(*this), hostSetsPerPriority()).WillByDefault(ReturnRef(host_sets_));
  ON_CALL(*this, addMemberUpdateCb(_))
      .WillByDefault(Invoke([this](PrioritySet::MemberUpdateCb cb) -> Common::CallbackHandlePtr {
        return member_update_cb_helper_.add(cb);
      }));
  ON_CALL(*this, addPriorityUpdateCb(_))
      .WillByDefault(Invoke([this](PrioritySet::PriorityUpdateCb cb) -> Common::CallbackHandlePtr {
        return priority_update_cb_helper_.add(cb);
      }));
  ON_CALL(*this, crossPriorityHostMap()).WillByDefault(ReturnPointee(&cross_priority_host_map_));
  ON_CALL(testing::Const(*this), refreshPartition(_))
      .WillByDefault(Invoke([this](uint32_t priority) {
    if (priority >= host_sets_.size()) {
      return;
    }
    auto* hs = static_cast<MockHostSet*>(host_sets_[priority].get());
    hs->healthy_hosts_.clear();
    hs->degraded_hosts_.clear();
    hs->excluded_hosts_.clear();
    for (const auto& host : hs->hosts_) {
      if (host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC) ||
          host->healthFlagGet(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL) ||
          host->healthFlagGet(Host::HealthFlag::EDS_STATUS_DRAINING)) {
        hs->excluded_hosts_.push_back(host);
        continue;
      }
      if (host->coarseHealth() == Host::Health::Healthy) {
        hs->healthy_hosts_.push_back(host);
      } else if (host->coarseHealth() == Host::Health::Degraded) {
        hs->degraded_hosts_.push_back(host);
      }
    }
  }));
}

MockPrioritySet::~MockPrioritySet() = default;

HostSet& MockPrioritySet::getHostSet(uint32_t priority) {
  if (host_sets_.size() < priority + 1) {
    for (size_t i = host_sets_.size(); i <= priority; ++i) {
      auto host_set = new ::testing::NiceMock<MockHostSet>(i);
      host_sets_.push_back(HostSetPtr{host_set});
      member_update_cbs_.push_back(
          host_set->addMemberUpdateCb([this](uint32_t priority, const HostVector& hosts_added,
                                             const HostVector& hosts_removed) {
            runUpdateCallbacks(priority, hosts_added, hosts_removed);
            return absl::OkStatus();
          }));
    }
  }
  return *host_sets_[priority];
}

void MockPrioritySet::runUpdateCallbacks(uint32_t priority, const HostVector& hosts_added,
                                         const HostVector& hosts_removed) {
  priority_update_cb_helper_.runCallbacks(priority, hosts_added, hosts_removed);
  member_update_cb_helper_.runCallbacks(hosts_added, hosts_removed);
}

} // namespace Upstream

} // namespace Envoy
