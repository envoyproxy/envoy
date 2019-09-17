#pragma once

#include "envoy/config/cluster/aggregate/cluster.pb.h"
#include "envoy/config/cluster/aggregate/cluster.pb.validate.h"

#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/clusters/aggregate/lb_context.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

class Cluster : public Upstream::BaseDynamicClusterImpl, Upstream::ClusterUpdateCallbacks {
public:
  Cluster(const envoy::api::v2::Cluster& cluster,
          const envoy::config::cluster::aggregate::ClusterConfig& config,
          Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime,
          Runtime::RandomGenerator& random,
          Server::Configuration::TransportSocketFactoryContext& factory_context,
          Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  Upstream::Cluster::InitializePhase initializePhase() const override {
    return Upstream::Cluster::InitializePhase::Secondary;
  }

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) override;
  void onClusterRemoval(const std::string& cluster_name) override;

  void refresh() {
    refresh([](const std::string&) { return false; });
  }

private:
  // Upstream::ClusterImplBase
  void startPreInit() override;

  void refresh(std::function<bool(const std::string&)>&& deleting);
  std::pair<Upstream::PrioritySetImpl,
            std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>>>
  linearizePrioritySet(std::function<bool(const std::string&)> deleting);

  class LoadBalancerImpl : public Upstream::LoadBalancerBase {
  public:
    LoadBalancerImpl(Cluster& parent,
                     std::pair<Upstream::PrioritySetImpl,
                               std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>>>&&
                         priority_setting)
        : Upstream::LoadBalancerBase(priority_setting.first, parent.info()->stats(),
                                     parent.runtime_, parent.random_, parent.info()->lbConfig()),
          priority_to_cluster_(std::move(priority_setting.second)) {}

    // Upstream::LoadBalancer
    Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;

    // Upstream::LoadBalancerBase
    Upstream::HostConstSharedPtr chooseHostOnce(Upstream::LoadBalancerContext*) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE
    }

  private:
    std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>> priority_to_cluster_;
  };

  using LoadBalancerSharedPtr = std::shared_ptr<LoadBalancerImpl>;
  LoadBalancerSharedPtr load_balancer_;

  std::vector<std::string> clusters_;
  Upstream::ClusterUpdateCallbacksHandlePtr handle_;
  Upstream::ClusterManager& cluster_manager_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;

public:
  LoadBalancerSharedPtr getLoadBalancer() const { return load_balancer_; }
};

class AggregateClusterLoadBalancer : public Upstream::LoadBalancer {
public:
  AggregateClusterLoadBalancer(const Cluster& cluster) : cluster_(cluster) {}

  // Upstream::LoadBalancer
  Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;

private:
  const Cluster& cluster_;
};

class AggregateLoadBalancerFactory : public Upstream::LoadBalancerFactory {
public:
  AggregateLoadBalancerFactory(const Cluster& cluster) : cluster_(cluster) {}

  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create() override {
    return std::make_unique<AggregateClusterLoadBalancer>(cluster_);
  }

private:
  const Cluster& cluster_;
};

class AggregateThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
public:
  AggregateThreadAwareLoadBalancer(const Cluster& cluster) : cluster_(cluster) {}

  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override {
    return std::make_shared<AggregateLoadBalancerFactory>(cluster_);
  }
  void initialize() override {}

private:
  const Cluster& cluster_;
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