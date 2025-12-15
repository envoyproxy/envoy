#include "source/extensions/clusters/composite/cluster.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.validate.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {

Cluster::Cluster(const envoy::config::cluster::v3::Cluster& cluster,
                 const envoy::extensions::clusters::composite::v3::ClusterConfig& config,
                 Upstream::ClusterFactoryContext& context, absl::Status& creation_status)
    : Upstream::ClusterImplBase(cluster, context, creation_status),
      cluster_manager_(context.serverFactoryContext().clusterManager()), clusters_([&config]() {
        auto clusters = std::make_shared<ClusterSet>();
        clusters->reserve(config.clusters_size());
        for (const auto& entry : config.clusters()) {
          clusters->push_back(entry.name());
        }
        return clusters;
      }()),
      overflow_option_(config.overflow_option()) {}

CompositeClusterLoadBalancer::CompositeClusterLoadBalancer(
    const Upstream::ClusterInfoConstSharedPtr& parent_info,
    Upstream::ClusterManager& cluster_manager, const ClusterSetConstSharedPtr& clusters,
    envoy::extensions::clusters::composite::v3::ClusterConfig::OverflowOption overflow_option)
    : parent_info_(parent_info), cluster_manager_(cluster_manager), clusters_(clusters),
      overflow_option_(overflow_option) {
  for (const auto& cluster : *clusters_) {
    auto tlc = cluster_manager_.getThreadLocalCluster(cluster);
    // It is possible when initializing the cluster, the included cluster doesn't exist. e.g., the
    // cluster could be added dynamically by xDS.
    if (tlc == nullptr) {
      continue;
    }

    // Add callback for clusters initialized before composite cluster.
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
          });
}

uint32_t
CompositeClusterLoadBalancer::getAttemptCount(Upstream::LoadBalancerContext* context) const {
  if (context == nullptr) {
    return 0;
  }

  // Get attempt count from stream info.
  auto* stream_info = context->requestStreamInfo();
  if (stream_info != nullptr && stream_info->attemptCount().has_value()) {
    return stream_info->attemptCount().value();
  }

  return 0;
}

absl::optional<size_t>
CompositeClusterLoadBalancer::mapAttemptToClusterIndex(uint32_t attempt_count) const {
  // Attempt count is 1-based in Envoy router.
  // First attempt (count = 1) uses first cluster (index 0).
  if (attempt_count == 0) {
    ENVOY_LOG(warn, "invalid attempt count 0 in composite cluster '{}'", parent_info_->name());
    return absl::nullopt;
  }

  const size_t cluster_index = attempt_count - 1;

  if (cluster_index < clusters_->size()) {
    return cluster_index;
  }

  // Handle overflow based on configuration.
  switch (overflow_option_) {
  case envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL:
    // Return nullopt to signal failure.
    return absl::nullopt;

  case envoy::extensions::clusters::composite::v3::ClusterConfig::USE_LAST_CLUSTER:
    // Use the last cluster in the list.
    return clusters_->size() - 1;

  case envoy::extensions::clusters::composite::v3::ClusterConfig::ROUND_ROBIN:
    // Round-robin through all clusters.
    return cluster_index % clusters_->size();

  default:
    // Default to FAIL behavior for unknown values.
    return absl::nullopt;
  }
}

Upstream::ThreadLocalCluster*
CompositeClusterLoadBalancer::getClusterByIndex(size_t cluster_index) const {
  if (cluster_index >= clusters_->size()) {
    ENVOY_LOG(debug, "cluster index {} exceeds available clusters {} in composite cluster '{}'",
              cluster_index, clusters_->size(), parent_info_->name());
    return nullptr;
  }

  const auto& cluster_name = (*clusters_)[cluster_index];
  auto tlc = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (tlc == nullptr) {
    ENVOY_LOG(debug, "cluster '{}' not found for composite cluster '{}'", cluster_name,
              parent_info_->name());
  }
  return tlc;
}

void CompositeClusterLoadBalancer::refresh(OptRef<const std::string> excluded_cluster) {
  UNREFERENCED_PARAMETER(excluded_cluster);
  // For composite cluster, we don't need to linearize priorities like the regular aggregate
  // cluster since each sub-cluster handles its own priorities independently.
  ENVOY_LOG(debug, "refreshing composite cluster '{}'", parent_info_->name());
}

void CompositeClusterLoadBalancer::onClusterAddOrUpdate(
    absl::string_view cluster_name, Upstream::ThreadLocalClusterCommand& get_cluster) {
  if (std::find(clusters_->begin(), clusters_->end(), cluster_name) != clusters_->end()) {
    ENVOY_LOG(debug, "adding or updating cluster '{}' for composite cluster '{}'", cluster_name,
              parent_info_->name());
    auto& cluster = get_cluster();
    refresh();
    addMemberUpdateCallbackForCluster(cluster);
  }
}

void CompositeClusterLoadBalancer::onClusterRemoval(const std::string& cluster_name) {
  // The onClusterRemoval callback is called before the thread local cluster is removed. There
  // will be a dangling pointer to the thread local cluster if the deleted cluster is not skipped
  // when we refresh the load balancer.
  if (std::find(clusters_->begin(), clusters_->end(), cluster_name) != clusters_->end()) {
    ENVOY_LOG(debug, "removing cluster '{}' from composite cluster '{}'", cluster_name,
              parent_info_->name());
    refresh(cluster_name);
  }
}

Upstream::HostSelectionResponse
CompositeClusterLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  // Extract attempt count from context.
  const uint32_t attempt_count = getAttemptCount(context);

  // Map attempt count to cluster index.
  const auto cluster_index_opt = mapAttemptToClusterIndex(attempt_count);
  if (!cluster_index_opt.has_value()) {
    ENVOY_LOG(debug, "no cluster available for attempt {} in composite cluster '{}'", attempt_count,
              parent_info_->name());
    return {nullptr};
  }

  const size_t cluster_index = cluster_index_opt.value();

  // Get the target cluster.
  auto* cluster = getClusterByIndex(cluster_index);
  if (cluster == nullptr) {
    ENVOY_LOG(debug, "cluster index {} not available for attempt {} in composite cluster '{}'",
              cluster_index, attempt_count, parent_info_->name());
    return {nullptr};
  }

  ENVOY_LOG(debug, "selecting cluster '{}' (index {}) for attempt {} in composite cluster '{}'",
            cluster->info()->name(), cluster_index, attempt_count, parent_info_->name());

  // Create wrapped context with cluster information.
  CompositeLoadBalancerContext composite_context(context, cluster_index);

  // Delegate to selected cluster's load balancer.
  return cluster->loadBalancer().chooseHost(&composite_context);
}

Upstream::HostConstSharedPtr
CompositeClusterLoadBalancer::peekAnotherHost(Upstream::LoadBalancerContext* context) {
  const uint32_t attempt_count = getAttemptCount(context);
  const auto cluster_index_opt = mapAttemptToClusterIndex(attempt_count);
  if (!cluster_index_opt.has_value()) {
    return nullptr;
  }

  const size_t cluster_index = cluster_index_opt.value();
  auto* cluster = getClusterByIndex(cluster_index);
  if (cluster == nullptr) {
    return nullptr;
  }

  CompositeLoadBalancerContext composite_context(context, cluster_index);
  return cluster->loadBalancer().peekAnotherHost(&composite_context);
}

absl::optional<Upstream::SelectedPoolAndConnection>
CompositeClusterLoadBalancer::selectExistingConnection(Upstream::LoadBalancerContext* context,
                                                       const Upstream::Host& host,
                                                       std::vector<uint8_t>& hash_key) {
  const uint32_t attempt_count = getAttemptCount(context);
  const auto cluster_index_opt = mapAttemptToClusterIndex(attempt_count);
  if (!cluster_index_opt.has_value()) {
    return absl::nullopt;
  }

  const size_t cluster_index = cluster_index_opt.value();
  auto* cluster = getClusterByIndex(cluster_index);
  if (cluster == nullptr) {
    return absl::nullopt;
  }

  CompositeLoadBalancerContext composite_context(context, cluster_index);
  return cluster->loadBalancer().selectExistingConnection(&composite_context, host, hash_key);
}

OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks>
CompositeClusterLoadBalancer::lifetimeCallbacks() {
  // Return empty for now. Could be enhanced to aggregate callbacks from sub-clusters.
  return {};
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
