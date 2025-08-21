#pragma once

#include "envoy/server/factory_context.h"
#include "envoy/upstream/upstream.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

class DfpLb {
public:
  virtual ~DfpLb() = default;
  virtual Upstream::HostConstSharedPtr findHostByName(const std::string& host) const PURE;
};

class DfpCluster {
public:
  virtual ~DfpCluster() = default;

  /**
   * @return the cluster enabled subCluster configuration or not.
   */
  virtual bool enableSubCluster() const PURE;

  /**
   * Create a full cluster config for the cluster_name, with the specified host and port.
   * @return true and nullptr when the subCluster with the specified cluster_name already
   *         created, or true and the created cluster config when it not exists and not
   *         reach the limitation of max_sub_clusters, otherwise, return false and nullptr.
   */
  virtual std::pair<bool, absl::optional<envoy::config::cluster::v3::Cluster>>
  createSubClusterConfig(const std::string& cluster_name, const std::string& host,
                         const int port) PURE;

  /**
   * Update the last used time of the subCluster with the specified cluster_name.
   * @return true if the subCluster is existing.
   */
  virtual bool touch(const std::string& cluster_name) PURE;
};

using DfpClusterSharedPtr = std::shared_ptr<DfpCluster>;
using DfpClusterWeakPtr = std::weak_ptr<DfpCluster>;

class DFPClusterStore : public Singleton::Instance {
public:
  // Load the dynamic forward proxy cluster from this store.
  DfpClusterSharedPtr load(std::string cluster_name);

  // Save the dynamic forward proxy cluster into this store.
  void save(const std::string cluster_name, DfpClusterSharedPtr cluster);

  // Remove the dynamic forward proxy cluster from this store.
  void remove(std::string cluster_name);

private:
  using ClusterMapType = absl::flat_hash_map<std::string, DfpClusterWeakPtr>;
  struct ClusterStoreType {
    ClusterMapType map_ ABSL_GUARDED_BY(mutex_);
    absl::Mutex mutex_;
  };

  ClusterStoreType& getClusterStore() { MUTABLE_CONSTRUCT_ON_FIRST_USE(ClusterStoreType); }
};

using DFPClusterStoreSharedPtr = std::shared_ptr<DFPClusterStore>;

class DFPClusterStoreFactory {
public:
  DFPClusterStoreFactory(Singleton::Manager& singleton_manager)
      : singleton_manager_(singleton_manager) {}
  DFPClusterStoreSharedPtr get();

private:
  Singleton::Manager& singleton_manager_;
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
