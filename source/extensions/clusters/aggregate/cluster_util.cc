#include "extensions/clusters/aggregate/cluster_util.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

Upstream::ThreadLocalCluster*
ClusterUtil::getThreadLocalCluster(Upstream::ClusterManager& cluster_manager,
                                   const std::string& name) {
  Upstream::ThreadLocalCluster* tlc = cluster_manager.get(name);
  if (tlc == nullptr) {
    throw EnvoyException(fmt::format("no thread local cluster with name {}", name));
  }

  return tlc;
}

std::pair<Upstream::PrioritySetImpl, std::vector<Upstream::ThreadLocalCluster*>>
ClusterUtil::linearizePrioritySet(Upstream::ClusterManager& cluster_manager,
                                  std::vector<std::string> clusters) {
  Upstream::PrioritySetImpl priority_set;
  std::vector<Upstream::ThreadLocalCluster*> priority_to_cluster;
  int next_priority = 0;

  for (const auto& cluster : clusters) {
    auto tlc = getThreadLocalCluster(cluster_manager, cluster);
    for (const auto& host_set : tlc->prioritySet().hostSetsPerPriority()) {
      if (!host_set->hosts().empty()) {
        priority_set.updateHosts(
            next_priority++, Upstream::HostSetImpl::updateHostsParams(*host_set),
            host_set->localityWeights(), host_set->hosts(), {}, host_set->overprovisioningFactor());
        priority_to_cluster.emplace_back(tlc);
      }
    }
  }

  return std::make_pair(std::move(priority_set), std::move(priority_to_cluster));
}

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy