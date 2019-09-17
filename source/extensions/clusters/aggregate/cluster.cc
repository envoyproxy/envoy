#include "extensions/clusters/aggregate/cluster.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

Cluster::Cluster(const envoy::api::v2::Cluster& cluster,
                 const envoy::config::cluster::aggregate::ClusterConfig& config,
                 Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime,
                 Runtime::RandomGenerator& random,
                 Server::Configuration::TransportSocketFactoryContext& factory_context,
                 Stats::ScopePtr&& stats_scope, bool added_via_api)
    : Upstream::BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                                       added_via_api),
      cluster_manager_(cluster_manager), runtime_(runtime), random_(random) {
  for (const auto& inner_cluster : config.clusters()) {
    clusters_.emplace_back(inner_cluster);
  }
}

std::pair<Upstream::PrioritySetImpl,
          std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>>>
Cluster::linearizePrioritySet(std::function<bool(const std::string&)> deleting) {
  Upstream::PrioritySetImpl priority_set;
  std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>> priority_to_cluster;
  int next_priority_after_linearizing = 0;

  // Linearize the priority set. e.g. for clusters [C_0, C_1, C_2] referred in aggregate cluster
  //    C_0 [P_0, P_1, P_2]
  //    C_1 [P_0, P_1]
  //    C_2 [P_0, P_1, P_2, P_3]
  // The linearization result is:
  //    [C_0.P_0, C_0.P_1, C_0.P_2, C_1.P_0, C_1.P_1, C_2.P_0, C_2.P_1, C_2.P_2, C_2.P_3]
  // and the traffic will be distributed among these priorities.
  for (const auto& cluster : clusters_) {
    if (deleting(cluster)) {
      continue;
    }
    auto tlc = cluster_manager_.get(cluster);
    if (tlc == nullptr) {
      continue;
    }

    int priority_in_current_cluster = 0;
    for (const auto& host_set : tlc->prioritySet().hostSetsPerPriority()) {
      if (!host_set->hosts().empty()) {
        priority_set.updateHosts(
            next_priority_after_linearizing++, Upstream::HostSetImpl::updateHostsParams(*host_set),
            host_set->localityWeights(), host_set->hosts(), {}, host_set->overprovisioningFactor());
        priority_to_cluster.emplace_back(std::make_pair(priority_in_current_cluster, tlc));
      }
      priority_in_current_cluster++;
    }
  }

  return std::make_pair(std::move(priority_set), std::move(priority_to_cluster));
}

void Cluster::startPreInit() {
  for (const auto cluster : clusters_) {
    auto tlc = cluster_manager_.get(cluster);
    if (tlc == nullptr) {
      continue;
    }

    // Add callback for clusters initialized before aggregate cluster.
    tlc->prioritySet().addMemberUpdateCb(
        [this](const Upstream::HostVector&, const Upstream::HostVector&) { refresh(); });
  }
  handle_ = cluster_manager_.addThreadLocalClusterUpdateCallbacks(*this);

  onPreInitComplete();
}

void Cluster::refresh(std::function<bool(const std::string&)> deleting) {
  auto pair = linearizePrioritySet(deleting);
  if (pair.first.hostSetsPerPriority().empty()) {
    if (load_balancer_ != nullptr) {
      load_balancer_.reset();
    }
  } else {
    load_balancer_ = std::make_shared<LoadBalancerImpl>(*this, std::move(pair));
  }
}

void Cluster::onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) {
  if (std::find(clusters_.begin(), clusters_.end(), cluster.info()->name()) != clusters_.end()) {
    refresh();
    cluster.prioritySet().addMemberUpdateCb(
        [this](const Upstream::HostVector&, const Upstream::HostVector&) { refresh(); });
  }
}

void Cluster::onClusterRemoval(const std::string& cluster_name) {
  //  The onClusterRemoval callback is called before the thread local cluster was removed. There
  //  will be a dangling pointer to the thread local cluster if delete cluster is not skipped.
  if (std::find(clusters_.begin(), clusters_.end(), cluster_name) != clusters_.end()) {
    refresh([&cluster_name](const std::string& c) { return cluster_name == c; });
  }
}

Upstream::HostConstSharedPtr
Cluster::LoadBalancerImpl::chooseHost(Upstream::LoadBalancerContext* context) {
  const auto priority_pair =
      choosePriority(random_.random(), per_priority_load_.healthy_priority_load_,
                     per_priority_load_.degraded_priority_load_);
  AggregateLoadBalancerContext aggregate_context(context, priority_pair.second,
                                                 priority_to_cluster_[priority_pair.first].first);
  return priority_to_cluster_[priority_pair.first].second->loadBalancer().chooseHost(
      &aggregate_context);
}

Upstream::HostConstSharedPtr
AggregateClusterLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  auto load_balancer = cluster_.getLoadBalancer();
  if (load_balancer) {
    return load_balancer->chooseHost(context);
  }
  return nullptr;
}

std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
ClusterFactory::createClusterWithConfig(
    const envoy::api::v2::Cluster& cluster,
    const envoy::config::cluster::aggregate::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  auto new_cluster = std::make_shared<Cluster>(
      cluster, proto_config, context.clusterManager(), context.runtime(), context.random(),
      socket_factory_context, std::move(stats_scope), context.addedViaApi());
  auto lb = std::make_unique<AggregateThreadAwareLoadBalancer>(*new_cluster);
  return std::make_pair(new_cluster, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy