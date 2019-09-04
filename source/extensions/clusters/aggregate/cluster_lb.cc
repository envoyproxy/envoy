#include "extensions/clusters/aggregate/cluster_lb.h"

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
  updatePrioritySetCallbacks(
      clusters_,
      [this](uint32_t, const Upstream::HostVector&, const Upstream::HostVector&) { refresh(); },
      [this](const Upstream::HostVector&, const Upstream::HostVector&) { refresh(); });

  handle_ = cluster_manager_.addThreadLocalClusterUpdateCallbacks(*this);
}

void AggregateClusterLoadBalancer::refresh() { refresh(clusters_); }

void AggregateClusterLoadBalancer::refresh(const std::vector<std::string>& clusters) {
  std::pair<Upstream::PrioritySetImpl,
            std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>>>
      pair = linearizePrioritySet(clusters);
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

std::pair<Upstream::PrioritySetImpl,
          std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>>>
AggregateClusterLoadBalancer::linearizePrioritySet(const std::vector<std::string>& clusters) {
  Upstream::PrioritySetImpl priority_set;
  std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>> priority_to_cluster;
  int next_priority_after_linearing = 0;

  // Linearize the priority set. e.g. for clusters [C_0, C_1, C_2] referred in aggregate cluster
  //    C_0 [P_0, P_1, P_2]
  //    C_1 [P_0, P_1]
  //    C_2 [P_0, P_1, P_2, P_3]
  // The linearization result is:
  //    [C_0.P_0, C_0.P_1, C_0.P_2, C_1.P_0, C_1.P_1, C_2.P_0, C_2.P_1, C_2.P_2, C_2.P_3]
  // and the traffic will be distributed among these priorities.
  for (const auto& cluster : clusters) {
    auto tlc = cluster_manager_.get(cluster);
    if (tlc == nullptr) {
      continue;
    }

    int priority_in_current_cluster = 0;
    for (const auto& host_set : tlc->prioritySet().hostSetsPerPriority()) {
      if (!host_set->hosts().empty()) {
        priority_set.updateHosts(
            next_priority_after_linearing++, Upstream::HostSetImpl::updateHostsParams(*host_set),
            host_set->localityWeights(), host_set->hosts(), {}, host_set->overprovisioningFactor());
        priority_to_cluster.emplace_back(std::make_pair(priority_in_current_cluster, tlc));
      }
      priority_in_current_cluster++;
    }
  }

  return std::make_pair(std::move(priority_set), std::move(priority_to_cluster));
}

void AggregateClusterLoadBalancer::updatePrioritySetCallbacks(
    const std::vector<std::string>& clusters, PriorityCb priority_cb, MemberCb member_cb) {
  for (const auto& cluster : clusters) {
    auto tlc = cluster_manager_.get(cluster);
    if (tlc == nullptr) {
      continue;
    }

    tlc->prioritySet().addPriorityUpdateCb(priority_cb);
    tlc->prioritySet().addMemberUpdateCb(member_cb);
  }
}

Upstream::HostConstSharedPtr
AggregateClusterLoadBalancer::LoadBalancerImpl::chooseHost(Upstream::LoadBalancerContext* context) {
  auto priority_pair = choosePriority(random_.random(), per_priority_load_.healthy_priority_load_,
                                      per_priority_load_.degraded_priority_load_);
  AggregateLoadBalancerContext aggregate_context(context, priority_pair.second,
                                                 priority_to_cluster_[priority_pair.first].first);
  return priority_to_cluster_[priority_pair.first].second->loadBalancer().chooseHost(
      &aggregate_context);
}

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy