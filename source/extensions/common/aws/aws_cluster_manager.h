#pragma once

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/singleton/manager.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/cleanup.h"
#include "source/common/init/target_impl.h"
#include "source/extensions/common/aws/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class AwsManagedClusterUpdateCallbacks {
public:
  virtual ~AwsManagedClusterUpdateCallbacks() = default;

  virtual void onClusterAddOrUpdate() PURE;
};

// class AwsManagedClusterUpdateCallbacksHandle {
// public:
//   virtual ~AwsManagedClusterUpdateCallbacksHandle() = default;
// };

class AwsManagedClusterUpdateCallbacksHandle
    : public RaiiListElement<AwsManagedClusterUpdateCallbacks*> {
public:
  AwsManagedClusterUpdateCallbacksHandle(AwsManagedClusterUpdateCallbacks& cb,
                                         std::list<AwsManagedClusterUpdateCallbacks*>& parent)
      : RaiiListElement<AwsManagedClusterUpdateCallbacks*>(parent, &cb) {}
};

using AwsManagedClusterUpdateCallbacksHandlePtr =
    std::unique_ptr<AwsManagedClusterUpdateCallbacksHandle>;

/**
 * Manages clusters for a number of async credentials provider instances
 */
class AwsCredentialsProviderClusterManager : public Envoy::Singleton::Instance,
                                             public Upstream::ClusterUpdateCallbacks {
  // Friend class for testing callbacks
  friend class AwsCredentialsProviderClusterManagerFriend;

public:
  // token and token_file_path are mutually exclusive. If token is not empty, token_file_path is
  // not used, and vice versa.
  AwsCredentialsProviderClusterManager(Server::Configuration::ServerFactoryContext& context);

  ~AwsCredentialsProviderClusterManager() override {
    ENVOY_LOG_MISC(debug, "******** acm destructor called");
  };

  absl::Status
  addManagedCluster(absl::string_view cluster_name,
                    const envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type,
                    absl::string_view uri);

  absl::StatusOr<AwsManagedClusterUpdateCallbacksHandlePtr>
  addManagedClusterUpdateCallbacks(absl::string_view cluster_name,
                                   AwsManagedClusterUpdateCallbacks& cb);
  absl::StatusOr<std::string> getUriFromClusterName(absl::string_view cluster_name);

private:
  void onClusterAddOrUpdate(absl::string_view, Upstream::ThreadLocalClusterCommand&) override;
  void onClusterRemoval(const std::string&) override;
  void createQueuedClusters();
  struct CredentialsProviderCluster {
    CredentialsProviderCluster(envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type,
                               std::string uri)
        : uri_(uri), cluster_type_(cluster_type){};

    std::string uri_;
    envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type_;
    // Atomic flag for cluster recreate
    std::atomic<bool> is_creating_ = false;
    std::list<AwsManagedClusterUpdateCallbacks*> update_callbacks_;
  };

  absl::flat_hash_map<std::string, std::unique_ptr<CredentialsProviderCluster>> managed_clusters_;
  std::atomic<bool> queue_clusters_ = true;
  Server::Configuration::ServerFactoryContext& context_;
  Upstream::ClusterUpdateCallbacksHandlePtr cm_handle_;
  Server::ServerLifecycleNotifier::HandlePtr shutdown_handle_;
  std::unique_ptr<Init::TargetImpl> init_target_;
};

using AwsCredentialsProviderClusterManagerPtr =
    std::shared_ptr<AwsCredentialsProviderClusterManager>;
using AwsCredentialsProviderClusterManagerOptRef = OptRef<AwsCredentialsProviderClusterManagerPtr>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
