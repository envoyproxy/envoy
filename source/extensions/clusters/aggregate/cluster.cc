#include "extensions/clusters/aggregate/cluster.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

Cluster::Cluster(const envoy::api::v2::Cluster& cluster,
                 const envoy::config::cluster::aggregate::ClusterConfig& config,
                 Runtime::Loader& runtime,
                 Server::Configuration::TransportSocketFactoryContext& factory_context,
                 Stats::ScopePtr&& stats_scope, bool added_via_api)
    : Upstream::BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                                       added_via_api) {
  for (const auto& cluster : config.clusters()) {
    clusters_.emplace_back(cluster);
  }
}

std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
ClusterFactory::createClusterWithConfig(
    const envoy::api::v2::Cluster& cluster,
    const envoy::config::cluster::aggregate::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  auto new_cluster =
      std::make_shared<Cluster>(cluster, proto_config, context.runtime(), socket_factory_context,
                                std::move(stats_scope), context.addedViaApi());
  auto lb = std::make_unique<AggregateThreadAwareLoadBalancer>(
      *new_cluster, context.clusterManager(), new_cluster->info()->stats(), context.runtime(),
      context.random(), new_cluster->info()->lbConfig());
  return std::make_pair(new_cluster, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy