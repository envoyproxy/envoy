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
  AwsManagedClusterUpdateCallbacksHandle(Server::Configuration::ServerFactoryContext& context,
                                         AwsManagedClusterUpdateCallbacks& cb,
                                         std::list<AwsManagedClusterUpdateCallbacks*>& parent)
      : RaiiListElement<AwsManagedClusterUpdateCallbacks*>(parent, &cb), context_(context) {}

public:
  Server::Configuration::ServerFactoryContext& context_;
};
using AwsManagedClusterUpdateCallbacksHandlePtr =
    std::unique_ptr<AwsManagedClusterUpdateCallbacksHandle>;

class AwsClusterManager {

public:
  virtual ~AwsClusterManager() = default;

  virtual absl::Status
  addManagedCluster(absl::string_view cluster_name,
                    const envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type,
                    absl::string_view uri) PURE;

  virtual absl::StatusOr<AwsManagedClusterUpdateCallbacksHandlePtr>
  addManagedClusterUpdateCallbacks(absl::string_view cluster_name,
                                   AwsManagedClusterUpdateCallbacks& cb) PURE;
  virtual absl::StatusOr<std::string> getUriFromClusterName(absl::string_view cluster_name) PURE;

private:
  virtual void createQueuedClusters() PURE;
};

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
class AwsClusterManagerImpl : public AwsClusterManager,
                              public Envoy::Singleton::Instance,
                              public Upstream::ClusterUpdateCallbacks {
  // Friend class for testing callbacks
  friend class AwsClusterManagerFriend;

public:
  AwsClusterManagerImpl(Server::Configuration::ServerFactoryContext& context);
  ~AwsClusterManagerImpl() override {
    if (cm_handle_) {
      // We exit last due to being pinned, so we must call cancel on the callbacks handle as it will
      // already be invalid by this time
      auto* handle = dynamic_cast<RaiiListElement<ClusterUpdateCallbacks*>*>(cm_handle_.get());
      handle->cancel();
    }
  };

  /**
   * Add a managed cluster to the aws cluster manager
   * @return absl::Status based on whether the cluster could be added to the cluster manager
   */

  absl::Status
  addManagedCluster(absl::string_view cluster_name,
                    const envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type,
                    absl::string_view uri) override;

  /**
   * Add a callback to be signaled when a managed cluster comes online. This is used to kick off
   * credential refresh
   * @return RAII handle for the callback
   */

  absl::StatusOr<AwsManagedClusterUpdateCallbacksHandlePtr>
  addManagedClusterUpdateCallbacks(absl::string_view cluster_name,
                                   AwsManagedClusterUpdateCallbacks& cb) override;
  absl::StatusOr<std::string> getUriFromClusterName(absl::string_view cluster_name) override;

private:
  // Callbacks for cluster manager
  void onClusterAddOrUpdate(absl::string_view, Upstream::ThreadLocalClusterCommand&) override;
  void onClusterRemoval(const std::string&) override;

  /**
   * Create all queued clusters, if we were unable to create them in real time due to envoy cluster
   * manager initialization
   */

  void createQueuedClusters() override;
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
  std::unique_ptr<Init::TargetImpl> init_target_;
};

using AwsClusterManagerImplPtr = std::shared_ptr<AwsClusterManagerImpl>;
using AwsClusterManagerPtr = std::shared_ptr<AwsClusterManager>;
using AwsClusterManagerOptRef = OptRef<AwsClusterManagerPtr>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
