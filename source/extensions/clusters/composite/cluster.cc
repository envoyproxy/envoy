#include "source/extensions/clusters/composite/cluster.h"

#include <memory>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {

Cluster::Cluster(const envoy::config::cluster::v3::Cluster& cluster,
                 const envoy::extensions::clusters::composite::v3::ClusterConfig& config,
                 Upstream::ClusterFactoryContext& context, absl::Status& creation_status)
    : Upstream::ClusterImplBase(cluster, context, creation_status),
      cluster_manager_(context.serverFactoryContext().clusterManager()),
      clusters_(std::make_shared<ClusterSet>(config.clusters().begin(), config.clusters().end())),
      overflow_option_(config.overflow_option()) {}

CompositeClusterLoadBalancer::CompositeClusterLoadBalancer(
    const Upstream::ClusterInfoConstSharedPtr& parent_info,
    Upstream::ClusterManager& cluster_manager, const ClusterSetConstSharedPtr& clusters,
    envoy::extensions::clusters::composite::v3::ClusterConfig::OverflowOption overflow_option)
    : parent_info_(parent_info), cluster_manager_(cluster_manager), clusters_(clusters),
      overflow_option_(overflow_option) {
  // Add member update callbacks for each configured cluster.
  for (const auto& cluster : *clusters_) {
    auto tlc = cluster_manager_.getThreadLocalCluster(cluster);
    // It is possible when initializing the cluster, the included cluster doesn't exist. e.g., the
    // cluster could be added dynamically by xDS.
    if (tlc == nullptr) {
      continue;
    }
    addMemberUpdateCallbackForCluster(*tlc);
  }
  refresh();
  handle_ = cluster_manager_.addThreadLocalClusterUpdateCallbacks(*this);
}

void CompositeClusterLoadBalancer::addMemberUpdateCallbackForCluster(
    Upstream::ThreadLocalCluster& thread_local_cluster) {
  member_update_cbs_[thread_local_cluster.info()->name()] =
      thread_local_cluster.prioritySet().addMemberUpdateCb(
          [this, target_cluster_info = thread_local_cluster.info()](const Upstream::HostVector&,
                                                                    const Upstream::HostVector&) {
            ENVOY_LOG(debug, "member update for cluster '{}' in composite cluster '{}'",
                      target_cluster_info->name(), parent_info_->name());
            refresh();
            return absl::OkStatus();
          });
}

uint32_t
CompositeClusterLoadBalancer::getRetryAttemptCount(Upstream::LoadBalancerContext* context) const {
  if (context == nullptr) {
    return 1;
  }

  auto* stream_info = context->requestStreamInfo();
  if (stream_info != nullptr && stream_info->attemptCount().has_value()) {
    return stream_info->attemptCount().value();
  }

  return 1;
}

absl::optional<size_t>
CompositeClusterLoadBalancer::mapRetryAttemptToClusterIndex(uint32_t retry_attempt) const {
  // Retry attempts are 1-based, but we need 0-based indexing.
  if (retry_attempt == 0) {
    ENVOY_LOG(debug, "composite cluster: invalid retry attempt 0");
    return absl::nullopt;
  }

  const size_t cluster_index = retry_attempt - 1;

  // Check if we have enough clusters for this attempt.
  if (cluster_index < clusters_->size()) {
    return cluster_index;
  }

  // Handle overflow based on configuration.
  switch (overflow_option_) {
  case envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL:
    ENVOY_LOG(debug, "composite cluster: retry attempt {} exceeds cluster count {}, failing",
              retry_attempt, clusters_->size());
    return absl::nullopt;

  case envoy::extensions::clusters::composite::v3::ClusterConfig::USE_LAST_CLUSTER:
    ENVOY_LOG(debug,
              "composite cluster: retry attempt {} exceeds cluster count {}, using last cluster",
              retry_attempt, clusters_->size());
    return clusters_->size() - 1;

  case envoy::extensions::clusters::composite::v3::ClusterConfig::ROUND_ROBIN: {
    const size_t round_robin_index = cluster_index % clusters_->size();
    ENVOY_LOG(debug, "composite cluster: retry attempt {} round-robin to cluster index {}",
              retry_attempt, round_robin_index);
    return round_robin_index;
  }

  default:
    ENVOY_LOG(debug, "composite cluster: unknown overflow option, failing");
    return absl::nullopt;
  }
}

Upstream::ThreadLocalCluster*
CompositeClusterLoadBalancer::getClusterByIndex(size_t cluster_index) const {
  if (cluster_index >= clusters_->size()) {
    ENVOY_LOG(debug, "composite cluster: cluster index {} out of bounds, cluster count: {}",
              cluster_index, clusters_->size());
    return nullptr;
  }

  const std::string& cluster_name = (*clusters_)[cluster_index];
  auto* cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (cluster == nullptr) {
    ENVOY_LOG(debug, "composite cluster: cluster '{}' not found", cluster_name);
  }
  return cluster;
}

void CompositeClusterLoadBalancer::refresh(OptRef<const std::string> excluded_cluster) {
  UNREFERENCED_PARAMETER(excluded_cluster);
  // Simple refresh implementation - no priority linearization needed like aggregate cluster
  // since we delegate to individual sub-cluster load balancers.
  ENVOY_LOG(debug, "refreshing composite cluster '{}'", parent_info_->name());
}

void CompositeClusterLoadBalancer::onClusterAddOrUpdate(
    absl::string_view cluster_name, Upstream::ThreadLocalClusterCommand& get_cluster) {
  if (std::find(clusters_->begin(), clusters_->end(), cluster_name) != clusters_->end()) {
    ENVOY_LOG(debug, "adding or updating cluster '{}' for composite cluster '{}'", cluster_name,
              parent_info_->name());
    auto& cluster = get_cluster();
    addMemberUpdateCallbackForCluster(cluster);
    refresh();
  }
}

void CompositeClusterLoadBalancer::onClusterRemoval(const std::string& cluster_name) {
  if (std::find(clusters_->begin(), clusters_->end(), cluster_name) != clusters_->end()) {
    ENVOY_LOG(debug, "removing cluster '{}' from composite cluster '{}'", cluster_name,
              parent_info_->name());
    member_update_cbs_.erase(cluster_name);
    refresh(cluster_name);
  }
}

Upstream::HostSelectionResponse
CompositeClusterLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  const uint32_t retry_attempt = getRetryAttemptCount(context);
  const auto cluster_index_opt = mapRetryAttemptToClusterIndex(retry_attempt);

  if (!cluster_index_opt.has_value()) {
    return {nullptr};
  }

  const size_t cluster_index = cluster_index_opt.value();
  auto* cluster = getClusterByIndex(cluster_index);
  if (cluster == nullptr) {
    return {nullptr};
  }

  ENVOY_LOG(debug, "composite cluster: selecting cluster '{}' for retry attempt {}",
            cluster->info()->name(), retry_attempt);

  CompositeLoadBalancerContext composite_context(context);
  return cluster->loadBalancer().chooseHost(&composite_context);
}

Upstream::HostConstSharedPtr
CompositeClusterLoadBalancer::peekAnotherHost(Upstream::LoadBalancerContext* context) {
  const uint32_t retry_attempt = getRetryAttemptCount(context);
  const auto cluster_index_opt = mapRetryAttemptToClusterIndex(retry_attempt);

  if (!cluster_index_opt.has_value()) {
    return nullptr;
  }

  auto* cluster = getClusterByIndex(cluster_index_opt.value());
  if (cluster == nullptr) {
    return nullptr;
  }

  CompositeLoadBalancerContext composite_context(context);
  return cluster->loadBalancer().peekAnotherHost(&composite_context);
}

absl::optional<Upstream::SelectedPoolAndConnection>
CompositeClusterLoadBalancer::selectExistingConnection(Upstream::LoadBalancerContext* context,
                                                       const Upstream::Host& host,
                                                       std::vector<uint8_t>& hash_key) {
  const uint32_t retry_attempt = getRetryAttemptCount(context);
  const auto cluster_index_opt = mapRetryAttemptToClusterIndex(retry_attempt);

  if (!cluster_index_opt.has_value()) {
    return absl::nullopt;
  }

  auto* cluster = getClusterByIndex(cluster_index_opt.value());
  if (cluster == nullptr) {
    return absl::nullopt;
  }

  CompositeLoadBalancerContext composite_context(context);
  return cluster->loadBalancer().selectExistingConnection(&composite_context, host, hash_key);
}

Upstream::LoadBalancerPtr CompositeLoadBalancerFactory::create(Upstream::LoadBalancerParams) {
  return std::make_unique<CompositeClusterLoadBalancer>(
      cluster_.info(), cluster_.cluster_manager_, cluster_.clusters_, cluster_.overflow_option_);
}

absl::StatusOr<std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
ClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::composite::v3::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context) {
  absl::Status creation_status = absl::OkStatus();
  auto new_cluster =
      std::shared_ptr<Cluster>(new Cluster(cluster, proto_config, context, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  auto lb = std::make_unique<CompositeThreadAwareLoadBalancer>(*new_cluster);
  return std::make_pair(new_cluster, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
