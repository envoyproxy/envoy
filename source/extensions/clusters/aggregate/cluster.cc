#include "extensions/clusters/aggregate/cluster.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

Cluster::Cluster(const envoy::api::v2::Cluster& cluster,
                 const envoy::config::cluster::aggregate::ClusterConfig& config,
                 Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime,
                 Server::Configuration::TransportSocketFactoryContext& factory_context,
                 Stats::ScopePtr&& stats_scope, bool added_via_api)
    : Upstream::BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                                       added_via_api),
      cluster_manager_(cluster_manager) {
  for (const auto& cluster : config.clusters()) {
    clusters_.emplace_back(cluster);
  }
}

Upstream::ThreadLocalCluster* Cluster::getThreadLocalCluster(const std::string& name) const {
  Upstream::ThreadLocalCluster* tlc = cluster_manager_.get(name);
  if (tlc == nullptr) {
    throw EnvoyException(fmt::format("no thread local cluster with name {}", name));
  }

  return tlc;
}

std::pair<Upstream::PrioritySetImpl, std::vector<Upstream::ThreadLocalCluster*>>
Cluster::linearizePrioritySet() const {
  Upstream::PrioritySetImpl priority_set;
  std::vector<Upstream::ThreadLocalCluster*> priority_to_cluster;
  int next_priority = 0;

  for (const auto& cluster : clusters_) {
    auto tlc = getThreadLocalCluster(cluster);
    for (const auto& host_set : tlc->prioritySet().hostSetsPerPriority()) {
      if (host_set->hosts().size() > 0) {
        priority_set.updateHosts(
            next_priority++, Upstream::HostSetImpl::updateHostsParams(*host_set),
            host_set->localityWeights(), host_set->hosts(), {}, host_set->overprovisioningFactor());
        priority_to_cluster.emplace_back(tlc);
      }
    }
  }

  return std::make_pair(std::move(priority_set), std::move(priority_to_cluster));
}

std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
ClusterFactory::createClusterWithConfig(
    const envoy::api::v2::Cluster& cluster,
    const envoy::config::cluster::aggregate::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  auto new_cluster = std::make_shared<Cluster>(cluster, proto_config, context.clusterManager(),
                                               context.runtime(), socket_factory_context,
                                               std::move(stats_scope), context.addedViaApi());
  auto lb = std::make_unique<AggregateThreadAwareLoadBalancer>(
      *new_cluster, new_cluster->info()->stats(), context.runtime(), context.random(),
      new_cluster->info()->lbConfig());
  return std::make_pair(new_cluster, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy