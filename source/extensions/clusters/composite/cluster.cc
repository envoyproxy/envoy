#include "source/extensions/clusters/composite/cluster.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.validate.h"

#include "source/common/common/assert.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {

CompositeClusterConfig::CompositeClusterConfig(
    const envoy::extensions::clusters::composite::v3::ClusterConfig& config) {
  clusters_.reserve(config.clusters_size());
  for (const auto& entry : config.clusters()) {
    clusters_.push_back(entry.name());
  }

  index_by_name_.reserve(clusters_.size());
  for (size_t i = 0; i < clusters_.size(); ++i) {
    // First occurrence wins if the same cluster is configured more than once.
    index_by_name_.emplace(clusters_[i], i);
  }

  if (config.has_order_metadata_key()) {
    order_metadata_key_.emplace(config.order_metadata_key());
  }
}

Cluster::Cluster(const envoy::config::cluster::v3::Cluster& cluster,
                 const envoy::extensions::clusters::composite::v3::ClusterConfig& config,
                 Upstream::ClusterFactoryContext& context, absl::Status& creation_status)
    : Upstream::ClusterImplBase(cluster, context, creation_status),
      cluster_manager_(context.serverFactoryContext().clusterManager()),
      config_(std::make_shared<const CompositeClusterConfig>(config)) {
  if (!creation_status.ok()) {
    return;
  }
  // Selecting itself would re-enter this load balancer with an unchanged attempt count and
  // recurse until the worker stack overflows.
  if (config_->index_by_name_.contains(cluster.name())) {
    creation_status = absl::InvalidArgumentError(
        absl::StrCat("composite cluster '", cluster.name(), "' cannot list itself in 'clusters'"));
  }
}

CompositeClusterLoadBalancer::CompositeClusterLoadBalancer(
    const Upstream::ClusterInfoConstSharedPtr& parent_info,
    Upstream::ClusterManager& cluster_manager, const CompositeClusterConfigConstSharedPtr& config)
    : parent_info_(parent_info), cluster_manager_(cluster_manager), config_(config) {
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

CompositeClusterLoadBalancer::SelectedCluster
CompositeClusterLoadBalancer::clusterForAttempt(Upstream::LoadBalancerContext* context,
                                                uint32_t attempt_count) const {
  // Attempt count is 1-based in Envoy router.
  // First attempt (count = 1) uses first cluster (index 0).
  if (attempt_count == 0) {
    ENVOY_LOG(warn, "invalid attempt count 0 in composite cluster '{}'", parent_info_->name());
    return {};
  }
  const size_t position = attempt_count - 1;

  if (config_->order_metadata_key_.has_value() && context != nullptr) {
    const StreamInfo::StreamInfo* stream_info = context->requestStreamInfo();
    if (stream_info != nullptr) {
      // metadataValue() returns the KIND_NOT_SET default instance for a missing namespace, a
      // missing path segment, or a path running through a non-struct.
      const Protobuf::Value& value = Envoy::Config::Metadata::metadataValue(
          &stream_info->dynamicMetadata(), config_->order_metadata_key_.value());

      if (value.kind_case() == Protobuf::Value::kListValue) {
        // Take the entry at `position`, skipping anything that is not a string naming a
        // configured cluster. Traversed rather than materialized, so this allocates nothing.
        SelectedCluster selected;
        size_t valid_entries = 0;
        for (const Protobuf::Value& entry : value.list_value().values()) {
          if (entry.kind_case() != Protobuf::Value::kStringValue) {
            continue;
          }
          const auto it = config_->index_by_name_.find(entry.string_value());
          if (it == config_->index_by_name_.end()) {
            continue;
          }
          if (valid_entries == position) {
            // Name the configuration's copy, so the view outlives the request.
            selected = {config_->clusters_[it->second], it->second};
            break;
          }
          ++valid_entries;
        }

        if (selected) {
          return selected;
        }
        if (valid_entries > 0) {
          // A usable override replaces the configured order wholesale, so an attempt past its end
          // fails rather than falling back to the configured order's tail.
          ENVOY_LOG(debug,
                    "attempt {} exceeds the {} entry order override for composite cluster '{}'",
                    attempt_count, valid_entries, parent_info_->name());
          return {};
        }
        ENVOY_LOG(debug,
                  "order override for composite cluster '{}' named no configured cluster; using "
                  "the configured order",
                  parent_info_->name());
      }
    }
  }

  // Absent, wrong type, non-list or fully filtered out metadata: use the configured order.
  if (position >= config_->clusters_.size()) {
    return {};
  }
  // Index by position, not by name: a name repeated in `clusters` would resolve to its first
  // occurrence rather than the entry this attempt selected.
  return {config_->clusters_[position], position};
}

CompositeClusterLoadBalancer::ResolvedCluster
CompositeClusterLoadBalancer::resolveCluster(Upstream::LoadBalancerContext* context) const {
  const uint32_t attempt_count = getAttemptCount(context);
  const SelectedCluster selected = clusterForAttempt(context, attempt_count);
  if (!selected) {
    ENVOY_LOG(debug, "no cluster available for attempt {} in composite cluster '{}'", attempt_count,
              parent_info_->name());
    return {};
  }

  auto* tlc = cluster_manager_.getThreadLocalCluster(selected.name_);
  if (tlc == nullptr) {
    ENVOY_LOG(debug, "cluster '{}' not found for composite cluster '{}'", selected.name_,
              parent_info_->name());
    return {};
  }

  ENVOY_LOG(debug, "selecting cluster '{}' (index {}) for attempt {} in composite cluster '{}'",
            selected.name_, selected.configured_index_, attempt_count, parent_info_->name());
  return {tlc, selected.configured_index_};
}

void CompositeClusterLoadBalancer::onClusterAddOrUpdate(
    absl::string_view cluster_name, Upstream::ThreadLocalClusterCommand& get_cluster) {
  UNREFERENCED_PARAMETER(get_cluster);
  if (config_->index_by_name_.contains(cluster_name)) {
    ENVOY_LOG(debug, "cluster '{}' added or updated for composite cluster '{}'", cluster_name,
              parent_info_->name());
  }
}

void CompositeClusterLoadBalancer::onClusterRemoval(absl::string_view cluster_name) {
  if (config_->index_by_name_.contains(cluster_name)) {
    ENVOY_LOG(debug, "cluster '{}' removed from composite cluster '{}'", cluster_name,
              parent_info_->name());
  }
}

Upstream::HostSelectionResponse
CompositeClusterLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  const ResolvedCluster resolved = resolveCluster(context);
  if (!resolved) {
    return {nullptr};
  }

  // Create wrapped context with cluster information.
  CompositeLoadBalancerContext composite_context(context, resolved.configured_index_);

  // Delegate to selected cluster's load balancer.
  return resolved.cluster_->loadBalancer().chooseHost(&composite_context);
}

Upstream::HostConstSharedPtr
CompositeClusterLoadBalancer::peekAnotherHost(Upstream::LoadBalancerContext* context) {
  const ResolvedCluster resolved = resolveCluster(context);
  if (!resolved) {
    return nullptr;
  }

  CompositeLoadBalancerContext composite_context(context, resolved.configured_index_);
  return resolved.cluster_->loadBalancer().peekAnotherHost(&composite_context);
}

std::optional<Upstream::SelectedPoolAndConnection>
CompositeClusterLoadBalancer::selectExistingConnection(Upstream::LoadBalancerContext* context,
                                                       const Upstream::Host& host,
                                                       std::vector<uint8_t>& hash_key) {
  const ResolvedCluster resolved = resolveCluster(context);
  if (!resolved) {
    return std::nullopt;
  }

  CompositeLoadBalancerContext composite_context(context, resolved.configured_index_);
  return resolved.cluster_->loadBalancer().selectExistingConnection(&composite_context, host,
                                                                    hash_key);
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
