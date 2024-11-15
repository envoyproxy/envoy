#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "test/extensions/load_balancing_policies/common/load_balancer_fuzz_base.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/simulated_time_system.h"

namespace Envoy {
namespace Upstream {
class ZoneAwareLoadBalancerFuzzBase : public Event::TestUsingSimulatedTime,
                                      public LoadBalancerFuzzBase {
public:
  ZoneAwareLoadBalancerFuzzBase(bool need_local_cluster, const std::string& random_bytestring)
      : random_bytestring_(random_bytestring) {
    if (need_local_cluster) {
      local_priority_set_ = std::make_shared<PrioritySetImpl>();
      local_priority_set_->getOrCreateHostSet(0);
    }
  }

  ~ZoneAwareLoadBalancerFuzzBase() override {
    // Clear out any set weights
    for (uint32_t priority_level = 0; priority_level < priority_set_.hostSetsPerPriority().size();
         ++priority_level) {
      MockHostSet& host_set = *priority_set_.getMockHostSet(priority_level);
      for (auto& host : host_set.hosts_) {
        host->weight(1);
      }
    }
    // This deletes the load balancer first. If constructed with a local priority set, load balancer
    // with reference local priority set on destruction. Since local priority set is in a base
    // class, it will be initialized second and thus destructed first. Thus, in order to avoid a use
    // after free, you must delete the load balancer before deleting the priority set.
    lb_.reset();
  }

  // These extend base class logic in order to handle local_priority_set_ if applicable.
  void
  initializeASingleHostSet(const test::common::upstream::SetupPriorityLevel& setup_priority_level,
                           const uint8_t priority_level, uint16_t& port) override;

  void initializeLbComponents(const test::common::upstream::LoadBalancerTestCase& input) override;

  void updateHealthFlagsForAHostSet(
      const uint64_t host_priority, const uint32_t num_healthy_hosts,
      const uint32_t num_degraded_hosts, const uint32_t num_excluded_hosts,
      const Protobuf::RepeatedField<Protobuf::uint32>& random_bytestring) override;

  void setupZoneAwareLoadBalancingSpecificLogic();

  void addWeightsToHosts();

  // A helper function to validate the SlowStart config values.
  // TODO(adisuissa): This should probably be reflected in the real LB config
  // constraints (see https://github.com/envoyproxy/envoy/pull/32506#issuecomment-1970047351).
  // For now, it is sufficient to validate these constraints only in the fuzzer.
  static bool
  validateSlowStart(const envoy::config::cluster::v3::Cluster_SlowStartConfig& slow_start_config,
                    uint32_t num_hosts);

  // If fuzzing Zone Aware Load Balancers, local priority set will get constructed sometimes. If not
  // constructed, a local_priority_set_.get() call will return a nullptr.
  std::shared_ptr<PrioritySetImpl> local_priority_set_;

private:
  // This bytestring will be iterated through representing randomness in order to choose
  // weights for hosts.
  const std::string random_bytestring_;
  uint32_t index_of_random_bytestring_ = 0;
};
} // namespace Upstream
} // namespace Envoy
