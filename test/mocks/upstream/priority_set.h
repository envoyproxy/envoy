#pragma once

#include "envoy/common/callback.h"
#include "envoy/upstream/upstream.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "host_set.h"

namespace Envoy {
namespace Upstream {
class MockPrioritySet : public PrioritySet {
public:
  MockPrioritySet();
  ~MockPrioritySet() override;

  HostSet& getHostSet(uint32_t priority);
  void runUpdateCallbacks(uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed);

  MOCK_METHOD(Common::CallbackHandlePtr, addMemberUpdateCb, (MemberUpdateCb callback), (const));
  MOCK_METHOD(Common::CallbackHandlePtr, addPriorityUpdateCb, (PriorityUpdateCb callback), (const));
  MOCK_METHOD(const std::vector<HostSetPtr>&, hostSetsPerPriority, (), (const));
  MOCK_METHOD(std::vector<HostSetPtr>&, hostSetsPerPriority, ());
  MOCK_METHOD(void, updateHosts,
              (uint32_t priority, UpdateHostsParams&& update_hosts_params,
               LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
               const HostVector& hosts_removed, uint64_t seed,
               absl::optional<bool> weighted_priority_health,
               absl::optional<uint32_t> overprovisioning_factor,
               HostMapConstSharedPtr cross_priority_host_map));
  MOCK_METHOD(void, batchHostUpdate, (BatchUpdateCb&));
  MOCK_METHOD(HostMapConstSharedPtr, crossPriorityHostMap, (), (const));

  MockHostSet* getMockHostSet(uint32_t priority) {
    getHostSet(priority); // Ensure the host set exists.
    return reinterpret_cast<MockHostSet*>(host_sets_[priority].get());
  }

  std::vector<HostSetPtr> host_sets_;
  std::vector<Common::CallbackHandlePtr> member_update_cbs_;
  Common::CallbackManager<const HostVector&, const HostVector&> member_update_cb_helper_;
  Common::CallbackManager<uint32_t, const HostVector&, const HostVector&>
      priority_update_cb_helper_;

  HostMapConstSharedPtr cross_priority_host_map_{std::make_shared<HostMap>()};
};
} // namespace Upstream
} // namespace Envoy
