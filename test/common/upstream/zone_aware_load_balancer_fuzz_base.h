#include "test/mocks/upstream/priority_set.h"

#include "load_balancer_fuzz_base.h"

namespace Envoy {
namespace Upstream {
class ZoneAwareLoadBalancerFuzzBase : public LoadBalancerFuzzBase {
public:
  ZoneAwareLoadBalancerFuzzBase(bool need_local_cluster, const std::string& random_bytestring)
      : LoadBalancerFuzzBase(), random_bytestring_(random_bytestring) {
    if (need_local_cluster) {
      local_priority_set_ = std::make_shared<NiceMock<MockPrioritySet>>();
      local_priority_set_->getMockHostSet(0);
    }
    // If you call local_priority_set_.get() and it's not constructed yet, it'll return a nullptr
  }

  // These extend base class logic in order to handle local_priority_set_ if applicable.
  void
  initializeASingleHostSet(const test::common::upstream::SetupPriorityLevel& setup_priority_level,
                           const uint8_t priority_level, uint16_t& port);
  void updateHealthFlagsForAHostSet(const uint64_t host_priority, const uint32_t num_healthy_hosts,
                                    const uint32_t num_degraded_hosts,
                                    const uint32_t num_excluded_hosts,
                                    const std::string random_bytestring);

  void setupZoneAwareLoadBalancingSpecificLogic();

  void addWeightsToHosts();

  // If fuzzing Zone Aware Load Balancers, local priority set will get constructed sometimes. If not
  // constructed, a local_priority_set_.get() call will return a nullptr.
  std::shared_ptr<NiceMock<MockPrioritySet>> local_priority_set_;

private:
  // This bytestring will be iterated through representing randomness in order to choose
  // weights for hosts.
  const std::string random_bytestring_;
  uint32_t index_of_random_bytestring_ = 0;
};
} // namespace Upstream
} // namespace Envoy
