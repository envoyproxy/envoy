#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Implementation of Upstream::Cluster for static clusters (clusters that have a fixed number of
 * hosts with resolved IP addresses).
 */
class StaticClusterImpl : public ClusterImplBase {
public:
  StaticClusterImpl(Server::Configuration::ServerFactoryContext& server_context,
                    const envoy::config::cluster::v3::Cluster& cluster, Runtime::Loader& runtime,
                    Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
                    Stats::ScopeSharedPtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  // ClusterImplBase
  void startPreInit() override;

  PriorityStateManagerPtr priority_state_manager_;
  uint32_t overprovisioning_factor_;
};

/**
 * Factory for StaticClusterImpl cluster.
 */
class StaticClusterFactory : public ClusterFactoryImplBase {
public:
  StaticClusterFactory() : ClusterFactoryImplBase("envoy.cluster.static") {}

private:
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> createClusterImpl(
      Server::Configuration::ServerFactoryContext& server_context,
      const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopeSharedPtr&& stats_scope) override;
};

DECLARE_FACTORY(StaticClusterFactory);

} // namespace Upstream
} // namespace Envoy
