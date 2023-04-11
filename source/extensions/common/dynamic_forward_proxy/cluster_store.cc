#include "source/extensions/common/dynamic_forward_proxy/cluster_store.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

Upstream::ClusterSharedPtr DFPClusterStore::load(const std::string cluster_name) {
  ClusterStoreType& clusterStore = getClusterStore();
  absl::ReaderMutexLock lock(&clusterStore.mutex_);
  auto it = clusterStore.map_.find(cluster_name);
  if (it != clusterStore.map_.end()) {
    return it->second.lock();
  }
  return nullptr;
}

void DFPClusterStore::save(const std::string cluster_name, Upstream::ClusterSharedPtr cluster) {
  ClusterStoreType& clusterStore = getClusterStore();
  absl::WriterMutexLock lock(&clusterStore.mutex_);
  clusterStore.map_[cluster_name] = std::move(cluster);
}

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
