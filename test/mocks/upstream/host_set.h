#pragma once

#include "envoy/upstream/upstream.h"

#include "source/common/common/callback_impl.h"
#include "source/common/upstream/upstream_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockHostSet : public HostSet {
public:
  MockHostSet(uint32_t priority = 0,
              uint32_t overprovisioning_factor = kDefaultOverProvisioningFactor);
  ~MockHostSet() override;

  void runCallbacks(const HostVector added, const HostVector removed) {
    THROW_IF_NOT_OK(member_update_cb_helper_.runCallbacks(priority(), added, removed));
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addMemberUpdateCb(PrioritySet::PriorityUpdateCb callback) {
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
  bool weightedPriorityHealth() const override { return weighted_priority_health_; }

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
  bool weighted_priority_health_{false};
  bool run_in_panic_mode_ = false;
};
} // namespace Upstream

} // namespace Envoy
