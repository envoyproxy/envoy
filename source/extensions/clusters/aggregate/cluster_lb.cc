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
      random_(random), common_config_(common_config) {}

void AggregateClusterLoadBalancer::initialize() {
  initialized_ = true;
  refresh();
  ClusterUtil::updatePrioritySetCallbacks(
      cluster_manager_, clusters_,
      [this](uint32_t, const Upstream::HostVector&, const Upstream::HostVector&) { refresh(); },
      [this](const Upstream::HostVector&, const Upstream::HostVector&) { refresh(); });

  handle_ = cluster_manager_.addThreadLocalClusterUpdateCallbacks(*this);
}

void AggregateClusterLoadBalancer::refresh() { refresh(clusters_); }

void AggregateClusterLoadBalancer::refresh(const std::vector<std::string>& clusters) {
  std::pair<Upstream::PrioritySetImpl,
            std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>>>
      pair = ClusterUtil::linearizePrioritySet(cluster_manager_, clusters);
  if (pair.first.hostSetsPerPriority().empty()) {
    load_balancer_ = nullptr;
  } else {
    load_balancer_ =
        std::make_unique<AggregateClusterLoadBalancer::LoadBalancerImpl>(*this, std::move(pair));
  }
}

void AggregateClusterLoadBalancer::onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) {
  if (std::find(clusters_.begin(), clusters_.end(), cluster.info()->name()) != clusters_.end()) {
    refresh();

    cluster.prioritySet().addPriorityUpdateCb(
        [this](uint32_t, const Upstream::HostVector&, const Upstream::HostVector&) { refresh(); });
    cluster.prioritySet().addMemberUpdateCb(
        [this](const Upstream::HostVector&, const Upstream::HostVector&) { refresh(); });
  }
}

void AggregateClusterLoadBalancer::onClusterRemoval(const std::string& cluster_name) {
  auto clusters = clusters_;
  auto it = std::find(clusters.begin(), clusters.end(), cluster_name);
  if (it != clusters.end()) {
    clusters.erase(it);
    refresh(clusters);
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