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

using PriorityContext = std::pair<Upstream::PrioritySetImpl,
                                  std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>>>;

class Cluster : public Upstream::BaseDynamicClusterImpl, Upstream::ClusterUpdateCallbacks {
public:
  Cluster(const envoy::api::v2::Cluster& cluster,
          const envoy::config::cluster::aggregate::ClusterConfig& config,
          Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime,
          Runtime::RandomGenerator& random,
          Server::Configuration::TransportSocketFactoryContext& factory_context,
          Stats::ScopePtr&& stats_scope, ThreadLocal::SlotAllocator& tls, bool added_via_api);

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

  std::vector<std::string> clusters_;
  Upstream::ClusterUpdateCallbacksHandlePtr handle_;
  Upstream::ClusterManager& cluster_manager_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  ThreadLocal::SlotPtr tls_;

private:
  // Upstream::ClusterImplBase
  void startPreInit() override;

  void refresh(std::function<bool(const std::string&)>&& skip_predicate);
  PriorityContext linearizePrioritySet(std::function<bool(const std::string&)> skip_predicate);
};

class LoadBalancerImpl : public Upstream::LoadBalancerBase {
public:
  LoadBalancerImpl(Cluster& cluster, PriorityContext& priority_context)
      : Upstream::LoadBalancerBase(priority_context.first, cluster.info()->stats(),
                                   cluster.runtime_, cluster.random_, cluster.info()->lbConfig()),
        priority_to_cluster_(priority_context.second) {}

  // Upstream::LoadBalancer
  Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;

  // Upstream::LoadBalancerBase
  Upstream::HostConstSharedPtr chooseHostOnce(Upstream::LoadBalancerContext*) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE
  }

private:
  std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>> priority_to_cluster_;
};

using LoadBalancerImplSharedPtr = std::shared_ptr<LoadBalancerImpl>;

// Load balancer used by each worker thread. It will be refreshed when clusters, hosts or priorities
// are updated.
class AggregateClusterLoadBalancer : public Upstream::LoadBalancer {
public:
  // Upstream::LoadBalancer
  Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;

  void refresh(LoadBalancerImplSharedPtr load_balancer) { load_balancer_ = load_balancer; }

private:
  LoadBalancerImplSharedPtr load_balancer_;
};

struct AggregateLoadBalancerFactory : public Upstream::LoadBalancerFactory {
  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create() override {
    return std::make_unique<AggregateClusterLoadBalancer>();
  }
};

// Thread aware load balancer created by the main thread.
struct AggregateThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override {
    return std::make_shared<AggregateLoadBalancerFactory>();
  }
  void initialize() override {}
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