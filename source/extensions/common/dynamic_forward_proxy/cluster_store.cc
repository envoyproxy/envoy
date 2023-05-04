#include "source/extensions/common/dynamic_forward_proxy/cluster_store.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

DfpClusterSharedPtr DFPClusterStore::load(const std::string cluster_name) {
  ClusterStoreType& clusterStore = getClusterStore();
  absl::ReaderMutexLock lock(&clusterStore.mutex_);
  auto it = clusterStore.map_.find(cluster_name);
  if (it != clusterStore.map_.end()) {
    return it->second.lock();
  }
  return nullptr;
}

void DFPClusterStore::save(const std::string cluster_name, DfpClusterSharedPtr cluster) {
  ClusterStoreType& clusterStore = getClusterStore();
  absl::WriterMutexLock lock(&clusterStore.mutex_);
  clusterStore.map_[cluster_name] = std::move(cluster);
}

void DFPClusterStore::remove(const std::string cluster_name) {
  ClusterStoreType& clusterStore = getClusterStore();
  absl::WriterMutexLock lock(&clusterStore.mutex_);
  clusterStore.map_.erase(cluster_name);
}

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
