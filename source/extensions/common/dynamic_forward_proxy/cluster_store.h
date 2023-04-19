#pragma once

#include "envoy/upstream/upstream.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

class DFPClusterStore {
public:
  // Load the dynamic forward proxy cluster from this store.
  static Upstream::DfpClusterSharedPtr load(std::string cluster_name);

  // Save the dynamic forward proxy cluster into this store.
  static void save(const std::string cluster_name, Upstream::DfpClusterSharedPtr cluster);

  // Remove the dynamic forward proxy cluster from this store.
  static void remove(std::string cluster_name);

private:
  using ClusterMapType = absl::flat_hash_map<std::string, Upstream::DfpClusterWeakPtr>;
  struct ClusterStoreType {
    ClusterMapType map_ ABSL_GUARDED_BY(mutex_);
    absl::Mutex mutex_;
  };

  static ClusterStoreType& getClusterStore() { MUTABLE_CONSTRUCT_ON_FIRST_USE(ClusterStoreType); }
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
