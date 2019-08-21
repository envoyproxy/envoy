#pragma once

#include "envoy/config/cluster/aggregate/cluster.pb.h"
#include "envoy/config/cluster/aggregate/cluster.pb.validate.h"

#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/clusters/aggregate/cluster_lb.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

class Cluster : public Upstream::BaseDynamicClusterImpl {
public:
  Cluster(const envoy::api::v2::Cluster& cluster,
          const envoy::config::cluster::aggregate::ClusterConfig& config, Runtime::Loader& runtime,
          Server::Configuration::TransportSocketFactoryContext& factory_context,
          Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  Upstream::Cluster::InitializePhase initializePhase() const override {
    // Try to postpone the initialization as late as possible.
    return Upstream::Cluster::InitializePhase::Secondary;
  }

  std::vector<ClusterVector> getClustersPerPriority() const { return clusters_per_priority_; };

private:
  // Upstream::ClusterImplBase
  void startPreInit() override {
    // Nothing to do during initialization. The initialization of clusters is delegated to cluster
    // manager.
    onPreInitComplete();
  }

  std::vector<ClusterVector> clusters_per_priority_;
};

struct AggregateLoadBalancerFactory : public Upstream::LoadBalancerFactory {
  AggregateLoadBalancerFactory(Cluster& cluster, Upstream::ClusterManager& cluster_manager,
                               Runtime::RandomGenerator& random)
      : cluster_(cluster), cluster_manager_(cluster_manager), random_(random) {}

  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create() override {
    return std::make_unique<AggregateClusterLoadBalancer>(cluster_manager_, random_,
                                                          cluster_.getClustersPerPriority());
  }

  Cluster& cluster_;
  Upstream::ClusterManager& cluster_manager_;
  Runtime::RandomGenerator& random_;
};

class AggregateThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
public:
  AggregateThreadAwareLoadBalancer(Cluster& cluster, Upstream::ClusterManager& cluster_manager,
                                   Runtime::RandomGenerator& random)
      : cluster_(cluster), cluster_manager_(cluster_manager), random_(random) {}

  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override {
    return std::make_shared<AggregateLoadBalancerFactory>(cluster_, cluster_manager_, random_);
  }
  void initialize() override {}

private:
  Cluster& cluster_;
  Upstream::ClusterManager& cluster_manager_;
  Runtime::RandomGenerator& random_;
};

/**
 * Factory for AggregateCluster
 */
class ClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                           envoy::config::cluster::aggregate::ClusterConfig> {
public:
  ClusterFactory()
      : ConfigurableClusterFactoryBase(Extensions::Clusters::ClusterTypes::get().Aggregate) {}

private:
  std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
  createClusterWithConfig(
      const envoy::api::v2::Cluster& cluster,
      const envoy::config::cluster::aggregate::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override;
};

DECLARE_FACTORY(ClusterFactory);

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy