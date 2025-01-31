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
 * Manages clusters for any number of credentials provider instances
 *
 * Credentials providers in async mode require clusters to be created so that they can use the async
 * http client to retrieve credentials. The aws cluster manager is responsible for creating these
 * clusters, and notifying a credential provider when a cluster comes on line so they can begin
 * retrieving credentials.
 *
 * - For InstanceProfileCredentialsProvider, a cluster is created with the uri of the instance
 * metadata service. Only one cluster is required for any number of instantiations of the aws
 * request signing extension.
 *
 * - For ContainerCredentialsProvider (including ECS and EKS), a cluster is created with the uri of
 * the container agent. Only one cluster is required for any number of instantiations of the aws
 * request signing extension.
 *
 * - For WebIdentityCredentialsProvider, a cluster is required for the STS service in any region
 * configured. There may be many WebIdentityCredentialsProvider instances instances configured, each
 * with their own region, or their own role ARN or role session name. The aws cluster manager will
 * maintain only a single cluster per region, and notify all relevant WebIdentityCredentialsProvider
 * instances when their cluster is ready.
 *
 * - For IAMRolesAnywhere, this behaves similarly to WebIdentityCredentialsProvider, where there may
 * be many instantiations of the credential provider for different roles, regions and profiles. The
 * aws cluster manager will dedupe these clusters as required.
 */
class AwsClusterManager : public Envoy::Singleton::Instance,
                          public Upstream::ClusterUpdateCallbacks {
  // Friend class for testing callbacks
  friend class AwsClusterManagerFriend;

public:
  AwsClusterManager(Server::Configuration::ServerFactoryContext& context);

  /**
   * Add a managed cluster to the aws cluster manager
   * @return absl::Status based on whether the cluster could be added to the cluster manager
   */

  absl::Status
  addManagedCluster(absl::string_view cluster_name,
                    const envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type,
                    absl::string_view uri);

  /**
   * Add a callback to be signaled when a managed cluster comes online. This is used to kick off
   * credential refresh
   * @return RAII handle for the callback
   */

  absl::StatusOr<AwsManagedClusterUpdateCallbacksHandlePtr>
  addManagedClusterUpdateCallbacks(absl::string_view cluster_name,
                                   AwsManagedClusterUpdateCallbacks& cb);
  absl::StatusOr<std::string> getUriFromClusterName(absl::string_view cluster_name);

private:
  // Callbacks for cluster manager
  void onClusterAddOrUpdate(absl::string_view, Upstream::ThreadLocalClusterCommand&) override;
  void onClusterRemoval(const std::string&) override;

  /**
   * Create all queued clusters, if we were unable to create them in real time due to envoy cluster
   * manager initialization
   */

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

using AwsClusterManagerPtr = std::shared_ptr<AwsClusterManager>;
using AwsClusterManagerOptRef = OptRef<AwsClusterManagerPtr>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
