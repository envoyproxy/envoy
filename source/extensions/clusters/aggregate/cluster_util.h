#pragma once

#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

using PriorityCb =
    std::function<void(uint32_t, const Upstream::HostVector&, const Upstream::HostVector&)>;

using MemberCb = std::function<void(const Upstream::HostVector&, const Upstream::HostVector&)>;

class ClusterUtil {
public:
  // Linearize the priority set of clusters into one priority set.
  // @param cluster_manager the cluster manager
  // @param clusters clusters in aggregate cluster
  // @return a pair of linearization result. First element if the priority set, second element if a
  // map from priority to cluster.
  static std::pair<Upstream::PrioritySetImpl,
                   std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>>>
  linearizePrioritySet(Upstream::ClusterManager& cluster_manager,
                       const std::vector<std::string>& clusters);

  // Update priority set callback
  // @param cluster_manager the cluster manager
  // @param clusters clusters in aggregate cluster
  // @param priority_cb priority callback
  // @param member_cb member callback
  static void updatePrioritySetCallbacks(Upstream::ClusterManager& cluster_manager,
                                         const std::vector<std::string>& clusters,
                                         PriorityCb priority_cb, MemberCb member_cb);
};
} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy