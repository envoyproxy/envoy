#pragma once

#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Implementation of Upstream::Cluster for static clusters (clusters that have a fixed number of
 * hosts with resolved IP addresses).
 */
class StaticClusterImpl : public ClusterImplBase {
public:
  StaticClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                    Server::Configuration::TransportSocketFactoryContext& factory_context,
                    Stats::ScopePtr&& stats_scope, bool added_via_api);

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
  StaticClusterFactory()
      : ClusterFactoryImplBase(Extensions::Clusters::ClusterTypes::get().Static) {}

private:
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
  createClusterImpl(const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context,
                    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
                    Stats::ScopePtr&& stats_scope) override;
};

} // namespace Upstream
} // namespace Envoy
