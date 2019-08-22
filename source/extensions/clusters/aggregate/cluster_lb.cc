#include "extensions/clusters/aggregate/cluster_lb.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

Upstream::HostConstSharedPtr
AggregateClusterLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  return chooseCluster()->loadBalancer().chooseHost(context);
}

Upstream::ThreadLocalCluster* AggregateClusterLoadBalancer::chooseCluster() const {
  auto priority_pair = choosePriority(random_.random(), per_priority_load_.healthy_priority_load_,
                                      per_priority_load_.degraded_priority_load_);

  return priority_to_cluster_[priority_pair.first];
}

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy