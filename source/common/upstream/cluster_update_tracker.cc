#include "source/common/upstream/cluster_update_tracker.h"

namespace Envoy {
namespace Upstream {

ClusterUpdateTracker::ClusterUpdateTracker(ClusterManager& cm, const std::string& cluster_name)
    : cluster_name_(cluster_name),
      cluster_update_callbacks_handle_(cm.addThreadLocalClusterUpdateCallbacks(*this)) {
  Upstream::ThreadLocalCluster* cluster = cm.getThreadLocalCluster(cluster_name_);
  if (cluster != nullptr) {
    thread_local_cluster_ = *cluster;
  }
}

void ClusterUpdateTracker::onClusterAddOrUpdate(ThreadLocalCluster& cluster) {
  if (cluster.info()->name() != cluster_name_) {
    return;
  }
  thread_local_cluster_ = cluster;
}

void ClusterUpdateTracker::onClusterRemoval(const std::string& cluster) {
  if (cluster != cluster_name_) {
    return;
  }
  thread_local_cluster_.reset();
}

} // namespace Upstream
} // namespace Envoy
