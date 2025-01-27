#include "source/extensions/common/aws/aws_cluster_manager.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

AwsCredentialsProviderClusterManager::AwsCredentialsProviderClusterManager(
    Server::Configuration::ServerFactoryContext& context)
    : context_(context) {
  ENVOY_LOG_MISC(debug, "******** acm constructor called");

  init_target_ = std::make_unique<Init::TargetImpl>("aws_cluster_manager", [this]() -> void {
    queue_clusters_.exchange(false);
    cm_handle_ = context_.clusterManager().addThreadLocalClusterUpdateCallbacks(*this);
    createQueuedClusters();

    init_target_->ready();
    init_target_.reset();
  });
  context_.initManager().add(*init_target_);
  // We're pinned, so ensure that we remove our cluster update callbacks before cluster manager
  // terminates
  shutdown_handle_ = context.lifecycleNotifier().registerCallback(
      Server::ServerLifecycleNotifier::Stage::ShutdownExit,
      [&](Event::PostCb) { cm_handle_.reset(); });
};

absl::StatusOr<AwsManagedClusterUpdateCallbacksHandlePtr>
AwsCredentialsProviderClusterManager::addManagedClusterUpdateCallbacks(
    absl::string_view cluster_name, AwsManagedClusterUpdateCallbacks& cb) {
  auto it = managed_clusters_.find(cluster_name);
  ENVOY_LOG_MISC(debug, "Adding callback for cluster {}", cluster_name);
  if (it == managed_clusters_.end()) {
    return absl::InvalidArgumentError("Cluster not found");
  }
  auto managed_cluster = it->second.get();
  // If the cluster is already alive, signal the callback immediately to start retrieving
  // credentials
  if (!managed_cluster->is_creating_) {
    ENVOY_LOG_MISC(debug, "Managed cluster {} is ready immediately, calling callback",
                   cluster_name);
    cb.onClusterAddOrUpdate();
  }
  return std::make_unique<AwsManagedClusterUpdateCallbacksHandle>(
      cb, managed_cluster->update_callbacks_);
}

void AwsCredentialsProviderClusterManager::onClusterAddOrUpdate(
    absl::string_view cluster_name, Upstream::ThreadLocalClusterCommand&) {

  // Mark our cluster as ready for use
  auto it = managed_clusters_.find(cluster_name);
  if (it != managed_clusters_.end()) {
    auto managed_cluster = it->second.get();
    managed_cluster->is_creating_.store(false);
    for (auto& cb : managed_cluster->update_callbacks_) {
      ENVOY_LOG_MISC(debug, "Managed cluster {} is ready, calling callback", cluster_name);
      cb->onClusterAddOrUpdate();
    }
  }
}

// If we have a cluster removal event, such as during cds update, recreate the cluster but leave the
// refresh timer as-is

void AwsCredentialsProviderClusterManager::onClusterRemoval(const std::string&) {}

void AwsCredentialsProviderClusterManager::createQueuedClusters() {
  for (const auto& it : managed_clusters_) {
    auto cluster_name = it.first;
    auto cluster_type = it.second->cluster_type_;
    auto uri = it.second->uri_;
    auto cluster = Utility::createInternalClusterStatic(cluster_name, cluster_type, uri);
    THROW_IF_NOT_OK(context_.clusterManager().addOrUpdateCluster(cluster, "", true).status());
  }
}

absl::Status AwsCredentialsProviderClusterManager::addManagedCluster(
    absl::string_view cluster_name,
    const envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type, absl::string_view uri) {

  auto it = managed_clusters_.find(cluster_name);
  if (it == managed_clusters_.end()) {
    auto new_cluster = std::make_unique<CredentialsProviderCluster>(cluster_type, std::string(uri));
    auto inserted = managed_clusters_.insert({std::string(cluster_name), std::move(new_cluster)});
    if (inserted.second) {
      it = inserted.first;
      it->second->is_creating_.store(true);
      ENVOY_LOG_MISC(debug, "Added cluster {} to list, cluster list len {}", cluster_name,
                     managed_clusters_.size());

      auto cluster = Utility::createInternalClusterStatic(cluster_name, cluster_type, uri);
      if (!queue_clusters_) {
        THROW_IF_NOT_OK(context_.clusterManager().addOrUpdateCluster(cluster, "", true).status());
      }
    }

    return absl::OkStatus();
  } else {
    ENVOY_LOG_MISC(debug, "Cluster {} already exists, not readding", cluster_name);
    return absl::AlreadyExistsError("Cluster already exists");
  }
}

absl::StatusOr<std::string>
AwsCredentialsProviderClusterManager::getUriFromClusterName(absl::string_view cluster_name) {
  ASSERT(!managed_clusters_.empty());
  for (const auto& it : managed_clusters_) {
    ENVOY_LOG_MISC(debug, "************* hashmap element {} searching for {} ***", it.first,
                   cluster_name);
  }

  auto it = managed_clusters_.find(cluster_name);
  if (it == managed_clusters_.end()) {
    return absl::InvalidArgumentError("Cluster not found");
  }
  return it->second->uri_;
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
