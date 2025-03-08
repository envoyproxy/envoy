#include "source/extensions/common/aws/aws_cluster_manager.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

AwsClusterManagerImpl::AwsClusterManagerImpl(Server::Configuration::ServerFactoryContext& context)
    : context_(context) {

  // If we are still initializing, defer cluster creation using an init target
  if (context_.initManager().state() == Envoy::Init::Manager::State::Initialized) {
    queue_clusters_.exchange(false);
    cm_handle_ = context_.clusterManager().addThreadLocalClusterUpdateCallbacks(*this);
  } else {
    init_target_ = std::make_unique<Init::TargetImpl>("aws_cluster_manager", [this]() -> void {
      queue_clusters_.exchange(false);
      cm_handle_ = context_.clusterManager().addThreadLocalClusterUpdateCallbacks(*this);
      createQueuedClusters();

      init_target_->ready();
      init_target_.reset();
    });
    context_.initManager().add(*init_target_);
  }
};

absl::StatusOr<AwsManagedClusterUpdateCallbacksHandlePtr>
AwsClusterManagerImpl::addManagedClusterUpdateCallbacks(absl::string_view cluster_name,
                                                        AwsManagedClusterUpdateCallbacks& cb) {
  auto it = managed_clusters_.find(cluster_name);
  ENVOY_LOG(debug, "Adding callback for cluster {}", cluster_name);
  if (it == managed_clusters_.end()) {
    return absl::InvalidArgumentError("Cluster not found");
  }
  auto managed_cluster = it->second.get();
  // If the cluster is already alive, signal the callback immediately to start retrieving
  // credentials
  if (!managed_cluster->is_creating_) {
    ENVOY_LOG(debug, "Managed cluster {} is ready immediately, calling callback", cluster_name);
    cb.onClusterAddOrUpdate();
    return absl::AlreadyExistsError("Cluster already online");
  }
  return std::make_unique<AwsManagedClusterUpdateCallbacksHandle>(
      context_, cb, managed_cluster->update_callbacks_);
}

void AwsClusterManagerImpl::onClusterAddOrUpdate(absl::string_view cluster_name,
                                                 Upstream::ThreadLocalClusterCommand&) {
  // Mark our cluster as ready for use
  auto it = managed_clusters_.find(cluster_name);
  if (it != managed_clusters_.end()) {
    auto managed_cluster = it->second.get();
    managed_cluster->is_creating_.store(false);
    for (auto& cb : managed_cluster->update_callbacks_) {
      ENVOY_LOG(debug, "Managed cluster {} is ready, calling callback", cluster_name);
      cb->onClusterAddOrUpdate();
    }
  }
}

// No removal handler required, as we are using avoid_cds_removal flag
void AwsClusterManagerImpl::onClusterRemoval(const std::string&){};

void AwsClusterManagerImpl::createQueuedClusters() {
  std::vector<std::string> failed_clusters;
  for (const auto& it : managed_clusters_) {
    auto cluster_name = it.first;
    auto cluster_type = it.second->cluster_type_;
    auto uri = it.second->uri_;
    auto cluster = Utility::createInternalClusterStatic(cluster_name, cluster_type, uri);
    auto status = context_.clusterManager().addOrUpdateCluster(cluster, "", true);
    if (!status.ok()) {
      ENVOY_LOG(debug, "Failed to add cluster {} to cluster manager: {}", cluster_name,
                status.status().ToString());
      failed_clusters.push_back(cluster_name);
    }
  }
  for (const auto& cluster_name : failed_clusters) {
    managed_clusters_.erase(cluster_name);
  }
}

absl::Status AwsClusterManagerImpl::addManagedCluster(
    absl::string_view cluster_name,
    const envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type, absl::string_view uri) {

  auto it = managed_clusters_.find(cluster_name);
  if (it == managed_clusters_.end()) {
    auto new_cluster = std::make_unique<CredentialsProviderCluster>(cluster_type, std::string(uri));
    auto inserted = managed_clusters_.insert({std::string(cluster_name), std::move(new_cluster)});
    if (inserted.second) {
      it = inserted.first;
      it->second->is_creating_.store(true);
      ENVOY_LOG(debug, "Added cluster {} to list, cluster list len {}", cluster_name,
                managed_clusters_.size());

      auto cluster = Utility::createInternalClusterStatic(cluster_name, cluster_type, uri);
      if (!queue_clusters_) {
        auto status = context_.clusterManager().addOrUpdateCluster(cluster, "", true);
        if (!status.ok()) {
          ENVOY_LOG(debug, "Failed to add cluster {} to cluster manager: {}", cluster_name,
                    status.status().ToString());
          managed_clusters_.erase(cluster_name);
          return status.status();
        }
      }
    }
    return absl::OkStatus();
  } else {
    ENVOY_LOG(debug, "Cluster {} already exists, not readding", cluster_name);
    return absl::AlreadyExistsError("Cluster already exists");
  }
}

absl::StatusOr<std::string>
AwsClusterManagerImpl::getUriFromClusterName(absl::string_view cluster_name) {
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
