#pragma once

#include "envoy/upstream/cluster_manager.h"

#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

class AggregateClusterLoadBalancer : public Upstream::LoadBalancerBase {
public:
  AggregateClusterLoadBalancer(std::vector<Upstream::ThreadLocalCluster*>&& priority_to_cluster,
                               const Upstream::PrioritySet& priority_set,
                               Upstream::ClusterStats& stats, Runtime::Loader& runtime,
                               Runtime::RandomGenerator& random,
                               const envoy::api::v2::Cluster::CommonLbConfig& common_config)
      : Upstream::LoadBalancerBase(priority_set, stats, runtime, random, common_config),
        priority_to_cluster_(std::move(priority_to_cluster)) {}

  // Upstream::LoadBalancer
  Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;

  // Upstream::LoadBalancerBase
  Upstream::HostConstSharedPtr chooseHostOnce(Upstream::LoadBalancerContext*) override {
    // The aggregate load balancer has implemented chooseHost, return nullptr directly.
    return nullptr;
  }

private:
  Upstream::ThreadLocalCluster* chooseCluster() const;
  std::vector<Upstream::ThreadLocalCluster*> priority_to_cluster_;
};

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy