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
  for (const auto& lb_cluster : config.lb_clusters()) {
    cluster_manager_.addOrUpdateCluster(lb_cluster.cluster(), /*version*/ "");
    priority_to_names_[lb_cluster.priority()].emplace_back(lb_cluster.cluster().name());
  }

  for (const auto& p : priority_to_names_) {
    clusters_per_priority_.emplace_back(p.second);
  }
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
      *new_cluster, context.clusterManager(), context.random());
  return std::make_pair(new_cluster, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy