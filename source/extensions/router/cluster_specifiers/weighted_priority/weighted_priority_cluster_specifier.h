#pragma once

#include <string>
#include <vector>

#include "envoy/extensions/router/cluster_specifiers/weighted_priority/v3/weighted_priority.pb.h"
#include "envoy/router/cluster_specifier_plugin.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace WeightedPriority {

using WeightedPriorityClusterSpecifierConfigProto = envoy::extensions::router::cluster_specifiers::
    weighted_priority::v3::WeightedPriorityClusterSpecifier;

// Selects a cluster by walking priority groups in order and making a weighted choice among the
// clusters of the first group that has a healthy cluster.
class WeightedPriorityClusterSpecifierPlugin : public Envoy::Router::ClusterSpecifierPlugin,
                                               Logger::Loggable<Logger::Id::router> {
public:
  WeightedPriorityClusterSpecifierPlugin(const WeightedPriorityClusterSpecifierConfigProto& config,
                                         Upstream::ClusterManager& cluster_manager);

  // Envoy::Router::ClusterSpecifierPlugin
  Envoy::Router::RouteConstSharedPtr route(Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
                                           const Http::RequestHeaderMap& headers,
                                           const StreamInfo::StreamInfo& stream_info,
                                           uint64_t random) const override;

private:
  struct WeightedCluster {
    std::string name_;
    uint32_t weight_{};
  };
  using PriorityGroup = std::vector<WeightedCluster>;

  // Weighted choice among the clusters of `group`. When `require_healthy` is set, clusters that
  // are unknown to the cluster manager or have no healthy hosts are excluded and the weights are
  // renormalized over the remainder. Returns an empty view when nothing is eligible.
  absl::string_view selectFromGroup(const PriorityGroup& group, uint64_t random,
                                    bool require_healthy) const;

  std::vector<PriorityGroup> priority_groups_;
  Upstream::ClusterManager& cluster_manager_;
};

} // namespace WeightedPriority
} // namespace Router
} // namespace Extensions
} // namespace Envoy
