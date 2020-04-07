#include "common/upstream/cluster_update_tracker.h"

namespace Envoy {
namespace Upstream {

ClusterUpdateTracker::ClusterUpdateTracker(ClusterManager& cm, const std::string& cluster_name)
    : cluster_name_(cluster_name),
      cluster_update_callbacks_handle_(cm.addThreadLocalClusterUpdateCallbacks(*this)) {
  Upstream::ThreadLocalCluster* cluster = cm.get(cluster_name_);
  cluster_info_ = cluster ? cluster->info() : nullptr;
}

void ClusterUpdateTracker::onClusterAddOrUpdate(ThreadLocalCluster& cluster) {
  if (cluster.info()->name() != cluster_name_) {
    return;
  }
  cluster_info_ = cluster.info();
}

void ClusterUpdateTracker::onClusterRemoval(const std::string& cluster) {
  if (cluster != cluster_name_) {
    return;
  }
  cluster_info_.reset();
}

} // namespace Upstream
} // namespace Envoy
