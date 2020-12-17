#pragma once

#include <utility>

#include "envoy/config/cluster/redis/redis_cluster.pb.h"
#include "envoy/config/cluster/redis/redis_cluster.pb.validate.h"
#include "envoy/stats/scope.h"

#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/upstream_impl.h"
#include "common/upstream/load_balancer_impl.h"

#include "source/extensions/clusters/redis/twem_cluster_lb.h"

#include "server/transport_socket_config_impl.h"

#include "extensions/clusters/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class TwemClusterFactory: public Upstream::ConfigurableClusterFactoryBase<envoy::config::cluster::redis::RedisClusterConfig> {
public:
  TwemClusterFactory()
      : ConfigurableClusterFactoryBase(Extensions::Clusters::ClusterTypes::get().Twem) {}

private:
  friend class TwemClusterTest;

  std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
  createClusterWithConfig(
            const envoy::config::cluster::v3::Cluster& cluster,
            const envoy::config::cluster::redis::RedisClusterConfig& proto_config,
            Upstream::ClusterFactoryContext& context,
            Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
            Stats::ScopePtr&& stats_scope) override;
};

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
