#pragma once

#include "envoy/upstream/cluster_manager.h"

#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

class AggregateClusterLoadBalancer : public Upstream::LoadBalancer,
                                     Upstream::ClusterUpdateCallbacks {
public:
  AggregateClusterLoadBalancer(Upstream::ClusterManager& cluster_manager,
                               const std::vector<std::string>& clusters,
                               Upstream::ClusterStats& stats, Runtime::Loader& runtime,
                               Runtime::RandomGenerator& random,
                               const envoy::api::v2::Cluster::CommonLbConfig& common_config);

  // Upstream::LoadBalancer
  Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override {
    return load_balancer_->chooseHost(context);
  }

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) override;
  void onClusterRemoval(const std::string& cluster_name) override;

private:
  void refreshLoadBalancer();

  class LoadBalancerImpl : public Upstream::LoadBalancerBase {
  public:
    LoadBalancerImpl(AggregateClusterLoadBalancer& parent,
                     const Upstream::PrioritySetImpl& priority_set,
                     std::vector<Upstream::ThreadLocalCluster*>&& priority_to_cluster)
        : Upstream::LoadBalancerBase(priority_set, parent.stats_, parent.runtime_, parent.random_,
                                     parent.common_config_),
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

  using LoadBalancerPtr = std::unique_ptr<LoadBalancerImpl>;

  LoadBalancerPtr load_balancer_;
  Upstream::ClusterManager& cluster_manager_;
  std::vector<std::string> clusters_;
  Upstream::ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  const envoy::api::v2::Cluster::CommonLbConfig& common_config_;
};

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy