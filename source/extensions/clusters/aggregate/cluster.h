#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.validate.h"

#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/clusters/aggregate/lb_context.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

using PriorityContext = std::pair<Upstream::PrioritySetImpl,
                                  std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>>>;

class Cluster : public Upstream::ClusterImplBase, Upstream::ClusterUpdateCallbacks {
public:
  Cluster(const envoy::config::cluster::v3::Cluster& cluster,
          const envoy::extensions::clusters::aggregate::v3::ClusterConfig& config,
          Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime,
          Runtime::RandomGenerator& random,
          Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
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

  Upstream::ClusterUpdateCallbacksHandlePtr handle_;
  Upstream::ClusterManager& cluster_manager_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  ThreadLocal::SlotPtr tls_;
  const std::vector<std::string> clusters_;

private:
  // Upstream::ClusterImplBase
  void startPreInit() override;

  void refresh(const std::function<bool(const std::string&)>& skip_predicate);
  PriorityContext
  linearizePrioritySet(const std::function<bool(const std::string&)>& skip_predicate);
};

// Load balancer used by each worker thread. It will be refreshed when clusters, hosts or priorities
// are updated.
class AggregateClusterLoadBalancer : public Upstream::LoadBalancer {
public:
  AggregateClusterLoadBalancer(
      Upstream::ClusterStats& stats, Runtime::Loader& runtime, Runtime::RandomGenerator& random,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
      : stats_(stats), runtime_(runtime), random_(random), common_config_(common_config) {}

  // Upstream::LoadBalancer
  Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;

private:
  // Use inner class to extend LoadBalancerBase. When initializing AggregateClusterLoadBalancer, the
  // priority set could be empty, we cannot initialize LoadBalancerBase when priority set is empty.
  class LoadBalancerImpl : public Upstream::LoadBalancerBase {
  public:
    LoadBalancerImpl(const PriorityContext& priority_context, Upstream::ClusterStats& stats,
                     Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                     const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
        : Upstream::LoadBalancerBase(priority_context.first, stats, runtime, random, common_config),
          priority_to_cluster_(priority_context.second) {}

    // Upstream::LoadBalancer
    Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;

    // Upstream::LoadBalancerBase
    Upstream::HostConstSharedPtr chooseHostOnce(Upstream::LoadBalancerContext*) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

  private:
    std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>> priority_to_cluster_;
  };

  using LoadBalancerImplPtr = std::unique_ptr<LoadBalancerImpl>;

  LoadBalancerImplPtr load_balancer_;
  Upstream::ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config_;

public:
  void refresh(const PriorityContext& priority_context) {
    if (!priority_context.first.hostSetsPerPriority().empty()) {
      load_balancer_ = std::make_unique<LoadBalancerImpl>(priority_context, stats_, runtime_,
                                                          random_, common_config_);
    } else {
      load_balancer_ = nullptr;
    }
  }
};

// Load balancer factory created by the main thread and will be called in each worker thread to
// create the thread local load balancer.
struct AggregateLoadBalancerFactory : public Upstream::LoadBalancerFactory {
  AggregateLoadBalancerFactory(const Cluster& cluster) : cluster_(cluster) {}
  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create() override {
    return std::make_unique<AggregateClusterLoadBalancer>(
        cluster_.info()->stats(), cluster_.runtime_, cluster_.random_, cluster_.info()->lbConfig());
  }

  const Cluster& cluster_;
};

// Thread aware load balancer created by the main thread.
struct AggregateThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
  AggregateThreadAwareLoadBalancer(const Cluster& cluster) : cluster_(cluster) {}

  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override {
    return std::make_shared<AggregateLoadBalancerFactory>(cluster_);
  }
  void initialize() override {}

  const Cluster& cluster_;
};

class ClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                           envoy::extensions::clusters::aggregate::v3::ClusterConfig> {
public:
  ClusterFactory()
      : ConfigurableClusterFactoryBase(Extensions::Clusters::ClusterTypes::get().Aggregate) {}

private:
  std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::aggregate::v3::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override;
};

DECLARE_FACTORY(ClusterFactory);

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
