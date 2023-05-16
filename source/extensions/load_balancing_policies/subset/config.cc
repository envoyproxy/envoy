#include "source/extensions/load_balancing_policies/subset/config.h"

#include "source/extensions/load_balancing_policies/subset/subset_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Subset {

Upstream::LoadBalancerPtr Factory::create(const Upstream::ClusterInfo& cluster,
                                          const Upstream::PrioritySet& priority_set,
                                          const Upstream::PrioritySet* local_priority_set,
                                          Runtime::Loader& runtime, Random::RandomGenerator& random,
                                          TimeSource& time_source) {
  auto child_lb_creator = std::make_unique<Upstream::LegacyChildLoadBalancerCreatorImpl>(
      cluster.lbType(), cluster.lbRingHashConfig(), cluster.lbMaglevConfig(),
      cluster.lbRoundRobinConfig(), cluster.lbLeastRequestConfig(), cluster.lbConfig());

  return std::make_unique<Upstream::SubsetLoadBalancer>(
      cluster.lbSubsetInfo(), std::move(child_lb_creator), priority_set, local_priority_set,
      cluster.lbStats(), cluster.statsScope(), runtime, random, time_source);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::NonThreadAwareLoadBalancerFactory);

} // namespace Subset
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
