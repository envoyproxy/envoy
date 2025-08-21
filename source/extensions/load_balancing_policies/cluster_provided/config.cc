#include "source/extensions/load_balancing_policies/cluster_provided/config.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace ClusterProvided {

Upstream::ThreadAwareLoadBalancerPtr Factory::create(OptRef<const Upstream::LoadBalancerConfig>,
                                                     const Upstream::ClusterInfo&,
                                                     const Upstream::PrioritySet&, Runtime::Loader&,
                                                     Random::RandomGenerator&, TimeSource&) {
  // Cluster provided load balancer has empty implementation. Because it is a special case to
  // tell the cluster to use the load balancer provided by the cluster.
  return nullptr;
};

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace ClusterProvided
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
