#pragma once

#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/load_balancer.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

using ClusterVector = std::vector<std::string>;

class AggregateClusterLoadBalancer : public Upstream::LoadBalancer {
public:
  AggregateClusterLoadBalancer(Upstream::ClusterManager& cluster_manager,
                               Runtime::RandomGenerator& random,
                               std::vector<ClusterVector>&& clusters_per_priority)
      : cluster_manager_(cluster_manager), random_(random),
        clusters_per_priority_(std::move(clusters_per_priority)) {}

  // Upstream::LoadBalancer
  Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;

private:
  int calculateAvailability(const std::string& cluster_name) const;

  Upstream::ClusterManager& cluster_manager_;
  Runtime::RandomGenerator& random_;
  std::vector<ClusterVector> clusters_per_priority_;
};

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy