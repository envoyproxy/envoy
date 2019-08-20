#include "extensions/clusters/aggregate/cluster_lb.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

Upstream::HostConstSharedPtr
AggregateClusterLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  int first = -1, total_avail = 0, size = clusters_per_priority_.size();
  int r = random_.random() % 100 + 1; // [1, 100]

  ClusterVector clusters;
  for (int p = 0; p < size; ++p) {
    ClusterVector cs = clusters_per_priority_[p];
    int avail = 0;
    // calculate availability for each cluster and update the availability of the priority
    for (const auto& cluster : cs) {
      int availability = calculateAvailability(cluster);
      avail = std::min(100, avail + availability);
    }

    // select the cluster set
    if (total_avail < r && total_avail + avail >= r) {
      clusters = cs;
    }

    // set first available cluster set
    if (avail != 0 && first == -1) {
      first = p;
    }

    total_avail += avail;
  }

  if (total_avail < 100) {
    // panic mode
    if (first == -1) {
      return nullptr;
    }

    clusters = clusters_per_priority_[first];
  }

  total_avail = 0;
  std::string cluster_name;
  for (const auto& c : clusters) {
    int avail = calculateAvailability(c);
    total_avail += avail;
    r = random_.random() % total_avail;
    if (total_avail != 0 && r < avail) {
      cluster_name = c;
    }
  }

  Upstream::ThreadLocalCluster* tlc = cluster_manager_.get(cluster_name);
  if (tlc == nullptr) {
    return nullptr;
  }

  return tlc->loadBalancer().chooseHost(context);
}

int AggregateClusterLoadBalancer::calculateAvailability(const std::string& cluster_name) const {
  Upstream::ThreadLocalCluster* tlc = cluster_manager_.get(cluster_name);
  if (tlc == nullptr) {
    return 0;
  }

  int total_hosts = 0, total_avail = 0;
  for (const auto& host_set : tlc->prioritySet().hostSetsPerPriority()) {
    total_avail += (host_set->healthyHosts().size() + host_set->degradedHosts().size()) *
                   host_set->overprovisioningFactor();
    total_hosts += host_set->hosts().size() - host_set->excludedHosts().size();
  }

  if (total_hosts == 0) {
    return 0;
  }

  return 100 * total_avail / total_hosts;
}

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy