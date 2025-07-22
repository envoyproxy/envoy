#include "source/extensions/clusters/aggregate_retry/cluster.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/clusters/aggregate_retry/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate_retry/v3/cluster.pb.validate.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace AggregateRetry {

Cluster::Cluster(const envoy::config::cluster::v3::Cluster& cluster,
                 const envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig& config,
                 Upstream::ClusterFactoryContext& context, absl::Status& creation_status)
    : Upstream::ClusterImplBase(cluster, context, creation_status),
      cluster_manager_(context.serverFactoryContext().clusterManager()),
      clusters_(std::make_shared<ClusterSet>(config.clusters().begin(), config.clusters().end())),
      retry_overflow_behavior_(config.retry_overflow_behavior()) {}

AggregateRetryClusterLoadBalancer::AggregateRetryClusterLoadBalancer(
    const Upstream::ClusterInfoConstSharedPtr& parent_info,
    Upstream::ClusterManager& cluster_manager, const ClusterSetConstSharedPtr& clusters,
    envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig::RetryOverflowBehavior
        retry_overflow_behavior)
    : parent_info_(parent_info), cluster_manager_(cluster_manager), clusters_(clusters),
      retry_overflow_behavior_(retry_overflow_behavior) {
  for (const auto& cluster : *clusters_) {
    auto tlc = cluster_manager_.getThreadLocalCluster(cluster);
    // It is possible when initializing the cluster, the included cluster doesn't exist. e.g., the
    // cluster could be added dynamically by xDS.
    if (tlc == nullptr) {
      continue;
    }

    // Add callback for clusters initialized before aggregate retry cluster.
    addMemberUpdateCallbackForCluster(*tlc);
  }
  refresh();
  handle_ = cluster_manager_.addThreadLocalClusterUpdateCallbacks(*this);
}

void AggregateRetryClusterLoadBalancer::addMemberUpdateCallbackForCluster(
    Upstream::ThreadLocalCluster& thread_local_cluster) {
  member_update_cbs_[thread_local_cluster.info()->name()] =
      thread_local_cluster.prioritySet().addMemberUpdateCb(
          [this, target_cluster_info = thread_local_cluster.info()](const Upstream::HostVector&,
                                                                    const Upstream::HostVector&) {
            ENVOY_LOG(debug, "member update for cluster '{}' in aggregate retry cluster '{}'",
                      target_cluster_info->name(), parent_info_->name());
            refresh();
            return absl::OkStatus();
          });
}

uint32_t AggregateRetryClusterLoadBalancer::getRetryAttemptCount(
    Upstream::LoadBalancerContext* context) const {
  if (context == nullptr) {
    return 0;
  }

  // Get retry attempt count from stream info.
  auto* stream_info = context->requestStreamInfo();
  if (stream_info != nullptr && stream_info->attemptCount().has_value()) {
    return stream_info->attemptCount().value();
  }

  return 0;
}

absl::optional<size_t>
AggregateRetryClusterLoadBalancer::mapRetryAttemptToClusterIndex(uint32_t retry_attempt) const {
  // Map retry attempt to cluster index. Retry attempts are 1-based in Envoy router.
  // First attempt (attempt 1) uses first cluster (index 0).
  if (retry_attempt == 0) {
    ENVOY_LOG(warn, "Invalid retry attempt 0 in aggregate retry cluster '{}'",
              parent_info_->name());
    return absl::nullopt;
  }

  const size_t cluster_index = retry_attempt - 1;

  if (cluster_index < clusters_->size()) {
    return cluster_index;
  }

  // Handle overflow based on configuration.
  switch (retry_overflow_behavior_) {
  case envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig::FAIL:
    // Return nullopt to signal failure.
    return absl::nullopt;
  case envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig::USE_LAST_CLUSTER:
    // Use the last cluster in the list.
    return clusters_->size() - 1;
  default:
    // Default to FAIL behavior.
    return absl::nullopt;
  }
}

Upstream::ThreadLocalCluster*
AggregateRetryClusterLoadBalancer::getClusterByIndex(size_t cluster_index) const {
  if (cluster_index >= clusters_->size()) {
    ENVOY_LOG(debug,
              "cluster index {} exceeds available clusters {} in aggregate retry cluster '{}'",
              cluster_index, clusters_->size(), parent_info_->name());
    return nullptr;
  }

  const auto& cluster_name = (*clusters_)[cluster_index];
  auto tlc = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (tlc == nullptr) {
    ENVOY_LOG(debug, "cluster '{}' not found for aggregate retry cluster '{}'", cluster_name,
              parent_info_->name());
  }
  return tlc;
}

void AggregateRetryClusterLoadBalancer::refresh(OptRef<const std::string> excluded_cluster) {
  UNREFERENCED_PARAMETER(excluded_cluster);
  // For aggregate retry cluster, we don't need to linearize priorities like the regular aggregate
  // cluster since each sub-cluster handles its own priorities independently.
  ENVOY_LOG(debug, "refreshing aggregate retry cluster '{}'", parent_info_->name());
}

void AggregateRetryClusterLoadBalancer::onClusterAddOrUpdate(
    absl::string_view cluster_name, Upstream::ThreadLocalClusterCommand& get_cluster) {
  if (std::find(clusters_->begin(), clusters_->end(), cluster_name) != clusters_->end()) {
    ENVOY_LOG(debug, "adding or updating cluster '{}' for aggregate retry cluster '{}'",
              cluster_name, parent_info_->name());
    auto& cluster = get_cluster();
    refresh();
    addMemberUpdateCallbackForCluster(cluster);
  }
}

void AggregateRetryClusterLoadBalancer::onClusterRemoval(const std::string& cluster_name) {
  // The ``onClusterRemoval()`` callback is called before the thread local cluster is removed. There
  // will be a dangling pointer to the thread local cluster if the deleted cluster is not skipped
  // when we refresh the load balancer.
  if (std::find(clusters_->begin(), clusters_->end(), cluster_name) != clusters_->end()) {
    ENVOY_LOG(debug, "removing cluster '{}' from aggregate retry cluster '{}'", cluster_name,
              parent_info_->name());
    refresh(cluster_name);
  }
}

Upstream::HostSelectionResponse
AggregateRetryClusterLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  // Extract retry attempt count from context.
  const uint32_t retry_attempt = getRetryAttemptCount(context);

  // Map retry attempt to cluster index.
  const auto cluster_index_opt = mapRetryAttemptToClusterIndex(retry_attempt);
  if (!cluster_index_opt.has_value()) {
    ENVOY_LOG(debug, "no cluster available for retry attempt {} in aggregate retry cluster '{}'",
              retry_attempt, parent_info_->name());
    return {nullptr};
  }

  const size_t cluster_index = cluster_index_opt.value();

  // Get the target cluster.
  auto* cluster = getClusterByIndex(cluster_index);
  if (cluster == nullptr) {
    ENVOY_LOG(debug,
              "cluster index {} not available for retry attempt {} in aggregate retry cluster '{}'",
              cluster_index, retry_attempt, parent_info_->name());
    return {nullptr};
  }

  ENVOY_LOG(
      debug,
      "selecting cluster '{}' (index {}) for retry attempt {} in aggregate retry cluster '{}'",
      cluster->info()->name(), cluster_index, retry_attempt, parent_info_->name());

  // Create wrapped context with cluster information.
  AggregateRetryLoadBalancerContext aggregate_retry_context(context, cluster_index);

  // Delegate to selected cluster's load balancer.
  return cluster->loadBalancer().chooseHost(&aggregate_retry_context);
}

Upstream::HostConstSharedPtr
AggregateRetryClusterLoadBalancer::peekAnotherHost(Upstream::LoadBalancerContext* context) {
  // For simplicity, delegate to chooseHost for now. Future optimization could cache the
  // selected cluster from the last chooseHost call.
  const uint32_t retry_attempt = getRetryAttemptCount(context);
  const auto cluster_index_opt = mapRetryAttemptToClusterIndex(retry_attempt);
  if (!cluster_index_opt.has_value()) {
    return nullptr;
  }

  const size_t cluster_index = cluster_index_opt.value();
  auto* cluster = getClusterByIndex(cluster_index);

  if (cluster == nullptr) {
    return nullptr;
  }

  AggregateRetryLoadBalancerContext aggregate_retry_context(context, cluster_index);
  return cluster->loadBalancer().peekAnotherHost(&aggregate_retry_context);
}

absl::optional<Upstream::SelectedPoolAndConnection>
AggregateRetryClusterLoadBalancer::selectExistingConnection(Upstream::LoadBalancerContext* context,
                                                            const Upstream::Host& host,
                                                            std::vector<uint8_t>& hash_key) {
  // Delegate to the appropriate cluster's load balancer.
  const uint32_t retry_attempt = getRetryAttemptCount(context);
  const auto cluster_index_opt = mapRetryAttemptToClusterIndex(retry_attempt);
  if (!cluster_index_opt.has_value()) {
    return absl::nullopt;
  }

  const size_t cluster_index = cluster_index_opt.value();
  auto* cluster = getClusterByIndex(cluster_index);

  if (cluster == nullptr) {
    return absl::nullopt;
  }

  AggregateRetryLoadBalancerContext aggregate_retry_context(context, cluster_index);
  return cluster->loadBalancer().selectExistingConnection(&aggregate_retry_context, host, hash_key);
}

OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks>
AggregateRetryClusterLoadBalancer::lifetimeCallbacks() {
  // Return empty for now. Could be enhanced to aggregate callbacks from sub-clusters.
  return {};
}

absl::StatusOr<std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
ClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::aggregate_retry::v3::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context) {
  absl::Status creation_status = absl::OkStatus();
  auto new_cluster =
      std::shared_ptr<Cluster>(new Cluster(cluster, proto_config, context, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  auto lb = std::make_unique<AggregateRetryThreadAwareLoadBalancer>(*new_cluster);
  return std::make_pair(new_cluster, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace AggregateRetry
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
