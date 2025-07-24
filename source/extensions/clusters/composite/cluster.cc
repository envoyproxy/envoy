#include "source/extensions/clusters/composite/cluster.h"

#include <memory>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.validate.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {

Cluster::Cluster(const envoy::config::cluster::v3::Cluster& cluster,
                 const envoy::extensions::clusters::composite::v3::ClusterConfig& config,
                 Upstream::ClusterFactoryContext& context, absl::Status& creation_status)
    : ClusterImplBase(cluster, context, creation_status), context_(context) {

  // Extract cluster names from ClusterEntry objects.
  clusters_ = std::make_unique<std::vector<std::string>>();
  for (const auto& cluster_entry : config.clusters()) {
    clusters_->push_back(cluster_entry.name());
  }

  // Store selection strategy and overflow option.
  selection_strategy_ = config.selection_strategy();
  retry_overflow_option_ = config.retry_overflow_option();
}

void CompositeConnectionLifetimeCallbacks::onConnectionOpen(
    Envoy::Http::ConnectionPool::Instance& pool, std::vector<uint8_t>& hash_key,
    const Network::Connection& connection) {
  for (auto* callback : callbacks_) {
    if (callback) {
      callback->onConnectionOpen(pool, hash_key, connection);
    }
  }
}

void CompositeConnectionLifetimeCallbacks::onConnectionDraining(
    Envoy::Http::ConnectionPool::Instance& pool, std::vector<uint8_t>& hash_key,
    const Network::Connection& connection) {
  for (auto* callback : callbacks_) {
    if (callback) {
      callback->onConnectionDraining(pool, hash_key, connection);
    }
  }
}

void CompositeConnectionLifetimeCallbacks::addCallback(
    Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks* callback) {
  callbacks_.push_back(callback);
}

void CompositeConnectionLifetimeCallbacks::removeCallback(
    Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks* callback) {
  callbacks_.erase(std::remove(callbacks_.begin(), callbacks_.end(), callback), callbacks_.end());
}

void CompositeConnectionLifetimeCallbacks::clearCallbacks() { callbacks_.clear(); }

CompositeClusterLoadBalancer::CompositeClusterLoadBalancer(
    const Upstream::ClusterInfo& cluster_info, Upstream::ClusterManager& cluster_manager,
    const std::vector<std::string>* clusters,
    envoy::extensions::clusters::composite::v3::ClusterConfig::SelectionStrategy selection_strategy,
    envoy::extensions::clusters::composite::v3::ClusterConfig::RetryOverflowOption
        retry_overflow_option)
    : cluster_info_(cluster_info), cluster_manager_(cluster_manager), clusters_(clusters),
      selection_strategy_(selection_strategy), retry_overflow_option_(retry_overflow_option) {
  UNREFERENCED_PARAMETER(cluster_info_);
  UNREFERENCED_PARAMETER(selection_strategy_);

  composite_callbacks_ = std::make_unique<CompositeConnectionLifetimeCallbacks>();

  // Add member update callbacks for each configured cluster.
  for (const auto& cluster_name : *clusters_) {
    if (auto* tlc = cluster_manager_.getThreadLocalCluster(cluster_name)) {
      addMemberUpdateCallbackForCluster(*tlc);
    }
  }
}

uint32_t
CompositeClusterLoadBalancer::getRetryAttemptCount(Upstream::LoadBalancerContext* context) const {
  if (context == nullptr) {
    return 1;
  }

  auto* stream_info = context->requestStreamInfo();
  if (!stream_info) {
    return 1;
  }
  auto attempt_count = stream_info->attemptCount();
  return attempt_count.has_value() ? attempt_count.value() : 1;
}

absl::optional<size_t>
CompositeClusterLoadBalancer::mapRetryAttemptToClusterIndex(size_t retry_attempt) const {
  // Retry attempts are 1-based, but we need 0-based indexing.
  if (retry_attempt == 0) {
    ENVOY_LOG(debug, "composite cluster: invalid retry attempt 0");
    return absl::nullopt;
  }

  size_t cluster_index = retry_attempt - 1;

  // Check if we have enough clusters for this attempt.
  if (cluster_index < clusters_->size()) {
    return cluster_index;
  }

  // Handle overflow based on configuration.
  switch (retry_overflow_option_) {
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
    size_t round_robin_index = cluster_index % clusters_->size();
    ENVOY_LOG(debug, "composite cluster: retry attempt {} round-robin to cluster index {}",
              retry_attempt, round_robin_index);
    return round_robin_index;
  }

  default:
    ENVOY_LOG(debug, "composite cluster: unknown retry overflow option, failing");
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

Upstream::HostSelectionResponse
CompositeClusterLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  uint32_t retry_attempt = getRetryAttemptCount(context);
  auto cluster_index_opt = mapRetryAttemptToClusterIndex(retry_attempt);

  if (!cluster_index_opt.has_value()) {
    return Upstream::HostSelectionResponse{nullptr};
  }

  size_t cluster_index = cluster_index_opt.value();
  auto* cluster = getClusterByIndex(cluster_index);
  if (cluster == nullptr) {
    return Upstream::HostSelectionResponse{nullptr};
  }

  ENVOY_LOG(debug, "composite cluster: retry attempt {} mapped to cluster '{}' (index {})",
            retry_attempt, cluster->info()->name(), cluster_index);

  // Create wrapper context with cluster index information.
  CompositeLoadBalancerContext composite_context(context, cluster_index);
  return cluster->loadBalancer().chooseHost(&composite_context);
}

Upstream::HostConstSharedPtr
CompositeClusterLoadBalancer::peekAnotherHost(Upstream::LoadBalancerContext* context) {
  uint32_t retry_attempt = getRetryAttemptCount(context);
  auto cluster_index_opt = mapRetryAttemptToClusterIndex(retry_attempt);

  if (!cluster_index_opt.has_value()) {
    return nullptr;
  }

  auto* cluster = getClusterByIndex(cluster_index_opt.value());
  if (cluster == nullptr) {
    return nullptr;
  }

  CompositeLoadBalancerContext composite_context(context, cluster_index_opt.value());
  return cluster->loadBalancer().peekAnotherHost(&composite_context);
}

absl::optional<Upstream::SelectedPoolAndConnection>
CompositeClusterLoadBalancer::selectExistingConnection(Upstream::LoadBalancerContext* context,
                                                       const Upstream::Host& host,
                                                       std::vector<uint8_t>& hash_key) {
  uint32_t retry_attempt = getRetryAttemptCount(context);
  auto cluster_index_opt = mapRetryAttemptToClusterIndex(retry_attempt);

  if (!cluster_index_opt.has_value()) {
    return absl::nullopt;
  }

  auto* cluster = getClusterByIndex(cluster_index_opt.value());
  if (cluster == nullptr) {
    return absl::nullopt;
  }

  CompositeLoadBalancerContext composite_context(context, cluster_index_opt.value());
  return cluster->loadBalancer().selectExistingConnection(&composite_context, host, hash_key);
}

void CompositeClusterLoadBalancer::addMemberUpdateCallbackForCluster(
    Upstream::ThreadLocalCluster& cluster) {
  member_update_cbs_.emplace_back(cluster.prioritySet().addMemberUpdateCb(
      [cluster_name =
           cluster.info()->name()](const Upstream::HostVector& added_hosts,
                                   const Upstream::HostVector& removed_hosts) -> absl::Status {
        ENVOY_LOG(debug, "composite cluster: member update for cluster '{}': {} added, {} removed",
                  cluster_name, added_hosts.size(), removed_hosts.size());
        return absl::OkStatus();
      }));
}

void CompositeClusterLoadBalancer::onClusterAddOrUpdate(
    absl::string_view cluster_name, Upstream::ThreadLocalClusterCommand& get_cluster) {
  UNREFERENCED_PARAMETER(cluster_name);
  UNREFERENCED_PARAMETER(get_cluster);
}

void CompositeClusterLoadBalancer::onClusterRemoval(const std::string& cluster_name) {
  ENVOY_LOG(debug, "composite cluster: cluster '{}' removed", cluster_name);
}

absl::StatusOr<std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
ClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::composite::v3::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context) {
  absl::Status creation_status = absl::OkStatus();
  auto cluster_impl = std::make_shared<Cluster>(cluster, proto_config, context, creation_status);
  RETURN_IF_NOT_OK(creation_status);
  auto lb = std::make_unique<CompositeThreadAwareLoadBalancer>(*cluster_impl);
  return std::make_pair(cluster_impl, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
