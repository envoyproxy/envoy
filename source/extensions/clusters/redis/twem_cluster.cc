#include "twem_cluster.h"

#include "common/upstream/eds.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
TwemClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::config::cluster::redis::RedisClusterConfig& ,
    Upstream::ClusterFactoryContext& context,
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
    Envoy::Stats::ScopePtr&& stats_scope) {
  if (!cluster.has_cluster_type() ||
      cluster.cluster_type().name() != Extensions::Clusters::ClusterTypes::get().Twem) {
    throw EnvoyException("twem cluster can only created with twem cluster type.");
  }

  auto first = std::make_shared<Upstream::EdsClusterImpl>(
    cluster, context.runtime(), socket_factory_context,
    std::move(stats_scope), context.addedViaApi());

  if (cluster.lb_policy() != envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    return std::make_pair(first, nullptr);
  }

  // TODO random
  return std::make_pair(first, std::make_unique<TwemClusterThreadAwareLoadBalancer>(
    first->prioritySet(), first->info()->stats(),
    first->info()->statsScope(), context.runtime(), context.api().randomGenerator(),
    first->info()->lbRingHashConfig(), first->info()->lbConfig()));
}

REGISTER_FACTORY(TwemClusterFactory, Upstream::ClusterFactory);

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
