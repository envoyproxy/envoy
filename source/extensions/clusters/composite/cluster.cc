#include "source/extensions/clusters/composite/cluster.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.validate.h"

#include "source/common/common/assert.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {
namespace {

// Runtime guard for skipping sub-clusters that cannot provide a host. When disabled the
// composite cluster keeps the legacy behavior of failing the attempt when the sub-cluster
// mapped to the current attempt has no host available.
constexpr absl::string_view SkipClustersWithoutHostsRuntimeKey =
    "envoy.reloadable_features.composite_cluster_skip_clusters_without_hosts";

} // namespace

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
      }()) {}

CompositeClusterLoadBalancer::CompositeClusterLoadBalancer(
    const Upstream::ClusterInfoConstSharedPtr& parent_info,
    Upstream::ClusterManager& cluster_manager, const ClusterSetConstSharedPtr& clusters)
    : parent_info_(parent_info), cluster_manager_(cluster_manager), clusters_(clusters) {
  handle_ = cluster_manager_.addThreadLocalClusterUpdateCallbacks(*this);
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

std::optional<size_t>
CompositeClusterLoadBalancer::mapAttemptToClusterIndex(uint32_t attempt_count) const {
  // Attempt count is 1-based in Envoy router.
  // First attempt (count = 1) uses first cluster (index 0).
  if (attempt_count == 0) {
    ENVOY_LOG(warn, "invalid attempt count 0 in composite cluster '{}'", parent_info_->name());
    return std::nullopt;
  }

  const size_t cluster_index = attempt_count - 1;

  if (cluster_index < clusters_->size()) {
    return cluster_index;
  }

  // Attempts exceed available clusters - fail the request.
  return std::nullopt;
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

void CompositeClusterLoadBalancer::onClusterAddOrUpdate(
    absl::string_view cluster_name, Upstream::ThreadLocalClusterCommand& get_cluster) {
  UNREFERENCED_PARAMETER(get_cluster);
  if (std::find(clusters_->begin(), clusters_->end(), cluster_name) != clusters_->end()) {
    ENVOY_LOG(debug, "cluster '{}' added or updated for composite cluster '{}'", cluster_name,
              parent_info_->name());
  }
}

void CompositeClusterLoadBalancer::onClusterRemoval(absl::string_view cluster_name) {
  if (std::find(clusters_->begin(), clusters_->end(), cluster_name) != clusters_->end()) {
    ENVOY_LOG(debug, "cluster '{}' removed from composite cluster '{}'", cluster_name,
              parent_info_->name());
  }
}

Upstream::HostSelectionResponse
CompositeClusterLoadBalancer::selectHostWithFailover(Upstream::LoadBalancerContext* context,
                                                     size_t start_index, uint32_t attempt_count) {
  const bool skip_clusters_without_hosts =
      Runtime::runtimeFeatureEnabled(SkipClustersWithoutHostsRuntimeKey);

  // Start at the cluster mapped to the current attempt and, when that cluster cannot provide a
  // host, advance to the following clusters. This allows failover to happen within a single
  // attempt when a sub-cluster has no healthy hosts, for example because DNS resolution returned
  // an empty endpoint list or because all of its hosts have been ejected.
  Upstream::HostSelectionResponse response{nullptr};
  for (size_t cluster_index = start_index; cluster_index < clusters_->size(); ++cluster_index) {
    auto* cluster = getClusterByIndex(cluster_index);
    if (cluster != nullptr) {
      ENVOY_LOG(debug, "selecting cluster '{}' (index {}) for attempt {} in composite cluster '{}'",
                cluster->info()->name(), cluster_index, attempt_count, parent_info_->name());

      // Create wrapped context with cluster information.
      CompositeLoadBalancerContext composite_context(context, cluster_index);

      // Delegate to selected cluster's load balancer.
      response = cluster->loadBalancer().chooseHost(&composite_context);

      // Either a host was selected, or asynchronous host selection is in flight and owns the
      // remainder of the selection process, so no other cluster may be tried.
      if (response.host != nullptr || response.cancelable != nullptr) {
        return response;
      }

      ENVOY_LOG(debug, "cluster '{}' (index {}) has no host available in composite cluster '{}'",
                cluster->info()->name(), cluster_index, parent_info_->name());
    } else {
      ENVOY_LOG(debug, "cluster index {} not available for attempt {} in composite cluster '{}'",
                cluster_index, attempt_count, parent_info_->name());
    }

    if (!skip_clusters_without_hosts) {
      break;
    }
  }

  return response;
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

  return selectHostWithFailover(context, cluster_index_opt.value(), attempt_count);
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

std::optional<Upstream::SelectedPoolAndConnection>
CompositeClusterLoadBalancer::selectExistingConnection(Upstream::LoadBalancerContext* context,
                                                       const Upstream::Host& host,
                                                       std::vector<uint8_t>& hash_key) {
  const uint32_t attempt_count = getAttemptCount(context);
  const auto cluster_index_opt = mapAttemptToClusterIndex(attempt_count);
  if (!cluster_index_opt.has_value()) {
    return std::nullopt;
  }

  const size_t cluster_index = cluster_index_opt.value();
  auto* cluster = getClusterByIndex(cluster_index);
  if (cluster == nullptr) {
    return std::nullopt;
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
