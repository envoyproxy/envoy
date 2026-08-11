#include "source/extensions/router/cluster_specifiers/weighted_priority/weighted_priority_cluster_specifier.h"

#include "source/common/router/delegating_route_impl.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace WeightedPriority {
namespace {

bool hasHealthyHosts(Upstream::ThreadLocalCluster& cluster) {
  for (const auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
    if (!host_set->healthyHosts().empty()) {
      return true;
    }
  }
  return false;
}

// Serves the parent route with the cluster replaced by the selected one.
class WeightedPriorityRouteEntry : public Envoy::Router::DelegatingRouteEntry {
public:
  WeightedPriorityRouteEntry(Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
                             absl::string_view cluster_name)
      : DelegatingRouteEntry(std::move(parent)), cluster_name_(cluster_name) {}

  const std::string& clusterName() const override { return cluster_name_; }

private:
  const std::string cluster_name_;
};

} // namespace

WeightedPriorityClusterSpecifierPlugin::WeightedPriorityClusterSpecifierPlugin(
    const WeightedPriorityClusterSpecifierConfigProto& config,
    Upstream::ClusterManager& cluster_manager)
    : cluster_manager_(cluster_manager) {
  priority_groups_.reserve(config.priority_groups_size());
  for (const auto& group : config.priority_groups()) {
    PriorityGroup entries;
    entries.reserve(group.clusters_size());
    for (const auto& cluster : group.clusters()) {
      entries.push_back({cluster.name(), cluster.weight()});
    }
    priority_groups_.push_back(std::move(entries));
  }
}

absl::string_view
WeightedPriorityClusterSpecifierPlugin::selectFromGroup(const PriorityGroup& group, uint64_t random,
                                                        bool require_healthy) const {
  // Sum the weights of the eligible clusters first, so the choice is renormalized over them rather
  // than over the configured total. Otherwise an excluded cluster's share would fall through to
  // whichever cluster happens to be last.
  uint64_t total_weight = 0;
  for (const auto& cluster : group) {
    if (require_healthy) {
      auto* thread_local_cluster = cluster_manager_.getThreadLocalCluster(cluster.name_);
      if (thread_local_cluster == nullptr || !hasHealthyHosts(*thread_local_cluster)) {
        continue;
      }
    }
    total_weight += cluster.weight_;
  }

  if (total_weight == 0) {
    return {};
  }

  uint64_t selector = random % total_weight;
  for (const auto& cluster : group) {
    if (require_healthy) {
      auto* thread_local_cluster = cluster_manager_.getThreadLocalCluster(cluster.name_);
      if (thread_local_cluster == nullptr || !hasHealthyHosts(*thread_local_cluster)) {
        continue;
      }
    }
    if (selector < cluster.weight_) {
      return cluster.name_;
    }
    selector -= cluster.weight_;
  }

  // Unreachable: the loops above visit the same clusters and `selector` is below their total.
  return {};
}

Envoy::Router::RouteConstSharedPtr WeightedPriorityClusterSpecifierPlugin::route(
    Envoy::Router::RouteEntryAndRouteConstSharedPtr parent, const Http::RequestHeaderMap&,
    const StreamInfo::StreamInfo&, uint64_t random) const {
  for (size_t i = 0; i < priority_groups_.size(); ++i) {
    const absl::string_view selected =
        selectFromGroup(priority_groups_[i], random, /*require_healthy=*/true);
    if (!selected.empty()) {
      ENVOY_LOG(debug, "weighted priority: selected cluster '{}' from group {}", selected, i);
      return std::make_shared<WeightedPriorityRouteEntry>(std::move(parent), selected);
    }
  }

  // No group had a healthy cluster. Choose from the last group regardless of health so the request
  // fails against a real cluster, and is counted against it, rather than being dropped as
  // unroutable.
  const absl::string_view fallback =
      selectFromGroup(priority_groups_.back(), random, /*require_healthy=*/false);
  ENVOY_LOG(debug, "weighted priority: no healthy cluster, falling back to '{}'", fallback);
  return std::make_shared<WeightedPriorityRouteEntry>(std::move(parent), fallback);
}

} // namespace WeightedPriority
} // namespace Router
} // namespace Extensions
} // namespace Envoy
