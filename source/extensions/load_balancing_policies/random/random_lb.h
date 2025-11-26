#pragma once

#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Random load balancer that picks a random host out of all hosts.
 */
class RandomLoadBalancer : public ZoneAwareLoadBalancerBase {
public:
  RandomLoadBalancer(
      const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
      Runtime::Loader& runtime, Random::RandomGenerator& random, uint32_t healthy_panic_threshold,
      const envoy::extensions::load_balancing_policies::random::v3::Random& random_config)
      : ZoneAwareLoadBalancerBase(
            priority_set, local_priority_set, stats, runtime, random, healthy_panic_threshold,
            LoadBalancerConfigHelper::localityLbConfigFromProto(random_config)) {}

  // Upstream::ZoneAwareLoadBalancerBase
  HostConstSharedPtr chooseHostOnce(LoadBalancerContext* context) override;
  HostConstSharedPtr peekAnotherHost(LoadBalancerContext* context) override;

protected:
  HostConstSharedPtr peekOrChoose(LoadBalancerContext* context, bool peek);
};

} // namespace Upstream
} // namespace Envoy
