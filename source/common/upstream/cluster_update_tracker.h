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

  bool exists() { return cluster_info_ != nullptr; }
  ClusterInfoConstSharedPtr info() { return cluster_info_; }

  // ClusterUpdateCallbacks
  void onClusterAddOrUpdate(ThreadLocalCluster& cluster) override;
  void onClusterRemoval(const std::string& cluster) override;

private:
  const std::string cluster_name_;
  const ClusterUpdateCallbacksHandlePtr cluster_update_callbacks_handle_;

  ClusterInfoConstSharedPtr cluster_info_;
};

} // namespace Upstream
} // namespace Envoy
