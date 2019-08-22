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

using ClusterVector = std::vector<std::string>;

class Cluster : public Upstream::BaseDynamicClusterImpl {
public:
  Cluster(const envoy::api::v2::Cluster& cluster,
          const envoy::config::cluster::aggregate::ClusterConfig& config,
          Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime,
          Server::Configuration::TransportSocketFactoryContext& factory_context,
          Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  Upstream::Cluster::InitializePhase initializePhase() const override {
    // Try to postpone the initialization as late as possible.
    return Upstream::Cluster::InitializePhase::Secondary;
  }

  Upstream::ThreadLocalCluster* getThreadLocalCluster(const std::string& name) const;
  std::pair<Upstream::PrioritySetImpl, std::vector<Upstream::ThreadLocalCluster*>>
  linearizePrioritySet() const;

private:
  // Upstream::ClusterImplBase
  void startPreInit() override {
    // Nothing to do during initialization. The initialization of clusters is delegated to cluster
    // manager.
    onPreInitComplete();
  }

  ClusterVector clusters_;
  Upstream::ClusterManager& cluster_manager_;
};

struct AggregateLoadBalancerFactory : public Upstream::LoadBalancerFactory {
  AggregateLoadBalancerFactory(Cluster& cluster, Upstream::ClusterStats& stats,
                               Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                               const envoy::api::v2::Cluster::CommonLbConfig& common_config)
      : cluster_(cluster), stats_(stats), runtime_(runtime), random_(random),
        common_config_(common_config) {}

  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create() override {
    std::pair<Upstream::PrioritySetImpl, std::vector<Upstream::ThreadLocalCluster*>> pair =
        cluster_.linearizePrioritySet();
    return std::make_unique<AggregateClusterLoadBalancer>(
        std::move(pair.second), std::move(pair.first), stats_, runtime_, random_, common_config_);
  }

  Cluster& cluster_;
  Upstream::ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  const envoy::api::v2::Cluster::CommonLbConfig& common_config_;
};

class AggregateThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
public:
  AggregateThreadAwareLoadBalancer(Cluster& cluster, Upstream::ClusterStats& stats,
                                   Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                                   const envoy::api::v2::Cluster::CommonLbConfig& common_config)
      : cluster_(cluster), stats_(stats), runtime_(runtime), random_(random),
        common_config_(common_config) {}

  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override {
    return std::make_shared<AggregateLoadBalancerFactory>(cluster_, stats_, runtime_, random_,
                                                          common_config_);
  }
  void initialize() override {}

private:
  Cluster& cluster_;
  Upstream::ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  const envoy::api::v2::Cluster::CommonLbConfig& common_config_;
};

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