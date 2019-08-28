#include "extensions/clusters/aggregate/cluster_lb.h"

#include "extensions/clusters/aggregate/cluster_util.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

AggregateClusterLoadBalancer::AggregateClusterLoadBalancer(
    Upstream::ClusterManager& cluster_manager, const std::vector<std::string>& clusters,
    Upstream::ClusterStats& stats, Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    const envoy::api::v2::Cluster::CommonLbConfig& common_config)
    : cluster_manager_(cluster_manager), clusters_(clusters), stats_(stats), runtime_(runtime),
      random_(random), common_config_(common_config) {
  refreshLoadBalancer();
  cluster_manager_.addThreadLocalClusterUpdateCallbacks(*this);
}

void AggregateClusterLoadBalancer::refreshLoadBalancer() {
  load_balancer_ = std::make_unique<AggregateClusterLoadBalancer::LoadBalancerImpl>(
      *this, ClusterUtil::linearizePrioritySet(cluster_manager_, clusters_));
}

void AggregateClusterLoadBalancer::onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) {
  auto callback = [this](uint32_t, const Upstream::HostVector&, const Upstream::HostVector&) {
    refreshLoadBalancer();
  };

  if (std::find(clusters_.begin(), clusters_.end(), cluster.info()->name()) != clusters_.end()) {
    refreshLoadBalancer();
    cluster.prioritySet().addPriorityUpdateCb(callback);
  }
}

void AggregateClusterLoadBalancer::onClusterRemoval(const std::string& cluster_name) {
  if (std::find(clusters_.begin(), clusters_.end(), cluster_name) != clusters_.end()) {
    refreshLoadBalancer();
  }
}

Upstream::HostConstSharedPtr
AggregateClusterLoadBalancer::LoadBalancerImpl::chooseHost(Upstream::LoadBalancerContext* context) {
  auto priority_pair = choosePriority(random_.random(), per_priority_load_.healthy_priority_load_,
                                      per_priority_load_.degraded_priority_load_);
  AggregateLoadBalancerContext aggr_context(context, priority_pair.second,
                                            priority_to_cluster_[priority_pair.first].first);
  return priority_to_cluster_[priority_pair.first].second->loadBalancer().chooseHost(&aggr_context);
}

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy