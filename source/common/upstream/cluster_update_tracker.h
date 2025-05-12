#pragma once

#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Upstream {

/**
 * Keeps track of cluster updates in order to spot addition and removal.
 *
 * Use this class as a performance optimization to avoid going through ClusterManager::get()
 * on the hot path.
 */
class ClusterUpdateTracker : public ClusterUpdateCallbacks {
public:
  ClusterUpdateTracker(ClusterManager& cm, const std::string& cluster_name);
  ThreadLocalClusterOptRef threadLocalCluster() { return thread_local_cluster_; };

  // ClusterUpdateCallbacks
  void onClusterAddOrUpdate(absl::string_view cluster_name,
                            ThreadLocalClusterCommand& get_cluster) override;
  void onClusterRemoval(const std::string& cluster) override;

private:
  const std::string cluster_name_;
  const ClusterUpdateCallbacksHandlePtr cluster_update_callbacks_handle_;

  ThreadLocalClusterOptRef thread_local_cluster_;
};

} // namespace Upstream
} // namespace Envoy
