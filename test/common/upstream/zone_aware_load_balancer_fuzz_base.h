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

  void setupZoneAwareLoadBalancingSpecificLogic();

  void addWeightsToHosts();

private:
  // This bytestring will be iterated through representing randomness in order to choose
  // weights for hosts.
  const std::string random_bytestring_;
  uint32_t index_of_random_bytestring_ = 0;
};
} // namespace Upstream
} // namespace Envoy
