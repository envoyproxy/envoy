#pragma once

#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

class ClusterUtil {
public:
  // Get thread local cluster with cluster name.
  // @param cluster_manager the cluster manager
  // @param name the cluster name
  // @return thread local cluster. Throw exception if cannot find the thread local cluster.
  static Upstream::ThreadLocalCluster*
  getThreadLocalCluster(Upstream::ClusterManager& cluster_manager, const std::string& name);

  // Linearize the priority set of clusters into one priority set.
  // @param cluster_manager the cluster manager
  // @param clusters clusters in aggregate cluster
  // @return a pair of linearization result. First element if the priority set, second element if a
  // map from priority to cluster.
  static std::pair<Upstream::PrioritySetImpl, std::vector<Upstream::ThreadLocalCluster*>>
  linearizePrioritySet(Upstream::ClusterManager& cluster_manager,
                       std::vector<std::string> clusters);
};
} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy