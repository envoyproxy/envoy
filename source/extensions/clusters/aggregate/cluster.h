#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.validate.h"
#include "envoy/thread_local/thread_local_object.h"

#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/clusters/aggregate/lb_context.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

using PriorityToClusterVector = std::vector<std::pair<uint32_t, Upstream::ThreadLocalCluster*>>;

// Maps pair(host_cluster_name, host_priority) to the linearized priority of the Aggregate cluster.
using ClusterAndPriorityToLinearizedPriorityMap =
    absl::flat_hash_map<std::pair<std::string, uint32_t>, uint32_t>;

struct PriorityContext {
  Upstream::PrioritySetImpl priority_set_;
  PriorityToClusterVector priority_to_cluster_;
  ClusterAndPriorityToLinearizedPriorityMap cluster_and_priority_to_linearized_priority_;
};

using PriorityContextPtr = std::unique_ptr<PriorityContext>;

class AggregateClusterLoadBalancer;

class Cluster : public Upstream::ClusterImplBase, Upstream::ClusterUpdateCallbacks {
public:
  Cluster(const envoy::config::cluster::v3::Cluster& cluster,
          const envoy::extensions::clusters::aggregate::v3::ClusterConfig& config,
          Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime,
          Random::RandomGenerator& random,
          Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
          Stats::ScopePtr&& stats_scope, ThreadLocal::SlotAllocator& tls, bool added_via_api);

  struct PerThreadLoadBalancer : public ThreadLocal::ThreadLocalObject {
    AggregateClusterLoadBalancer& get() {
      // We can refresh before the per-worker LB is created. One of these variants should hold
      // a non-null value.
      if (absl::holds_alternative<std::unique_ptr<AggregateClusterLoadBalancer>>(lb_)) {
        ASSERT(absl::get<std::unique_ptr<AggregateClusterLoadBalancer>>(lb_) != nullptr);
        return *absl::get<std::unique_ptr<AggregateClusterLoadBalancer>>(lb_);
      } else {
        ASSERT(absl::get<AggregateClusterLoadBalancer*>(lb_) != nullptr);
        return *absl::get<AggregateClusterLoadBalancer*>(lb_);
      }
    }

    // For aggregate cluster the per-thread LB is only created once. We need to own it so we
    // can pre-populate it before the LB is created and handed to the cluster.
    absl::variant<std::unique_ptr<AggregateClusterLoadBalancer>, AggregateClusterLoadBalancer*> lb_;
  };

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
  Random::RandomGenerator& random_;
  ThreadLocal::TypedSlot<PerThreadLoadBalancer> tls_;
  const std::vector<std::string> clusters_;

private:
  // Upstream::ClusterImplBase
  void startPreInit() override;

  void refresh(const std::function<bool(const std::string&)>& skip_predicate);
  PriorityContextPtr
  linearizePrioritySet(const std::function<bool(const std::string&)>& skip_predicate);
};

// Load balancer used by each worker thread. It will be refreshed when clusters, hosts or priorities
// are updated.
class AggregateClusterLoadBalancer : public Upstream::LoadBalancer {
public:
  AggregateClusterLoadBalancer(
      Upstream::ClusterStats& stats, Runtime::Loader& runtime, Random::RandomGenerator& random,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
      : stats_(stats), runtime_(runtime), random_(random), common_config_(common_config) {}

  // Upstream::LoadBalancer
  Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;
  // Prefetching not yet implemented for extensions.
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
    return nullptr;
  }

private:
  // Use inner class to extend LoadBalancerBase. When initializing AggregateClusterLoadBalancer, the
  // priority set could be empty, we cannot initialize LoadBalancerBase when priority set is empty.
  class LoadBalancerImpl : public Upstream::LoadBalancerBase {
  public:
    LoadBalancerImpl(const PriorityContext& priority_context, Upstream::ClusterStats& stats,
                     Runtime::Loader& runtime, Random::RandomGenerator& random,
                     const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
        : Upstream::LoadBalancerBase(priority_context.priority_set_, stats, runtime, random,
                                     common_config),
          priority_context_(priority_context) {}

    // Upstream::LoadBalancer
    Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;
    // Prefetching not yet implemented for extensions.
    Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
      return nullptr;
    }

    // Upstream::LoadBalancerBase
    Upstream::HostConstSharedPtr chooseHostOnce(Upstream::LoadBalancerContext*) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    absl::optional<uint32_t> hostToLinearizedPriority(const Upstream::HostDescription& host) const;

  private:
    const PriorityContext& priority_context_;
  };

  using LoadBalancerImplPtr = std::unique_ptr<LoadBalancerImpl>;

  LoadBalancerImplPtr load_balancer_;
  Upstream::ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
  const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config_;
  PriorityContextPtr priority_context_;

public:
  void refresh(PriorityContextPtr priority_context) {
    if (!priority_context->priority_set_.hostSetsPerPriority().empty()) {
      load_balancer_ = std::make_unique<LoadBalancerImpl>(*priority_context, stats_, runtime_,
                                                          random_, common_config_);
    } else {
      load_balancer_ = nullptr;
    }
    priority_context_ = std::move(priority_context);
  }
};

// Load balancer factory created by the main thread and will be called in each worker thread to
// create the thread local load balancer.
struct AggregateLoadBalancerFactory : public Upstream::LoadBalancerFactory {
  AggregateLoadBalancerFactory(const Cluster& cluster) : cluster_(cluster) {}
  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create() override {
    // See comments in PerThreadLoadBalancer above for why the follow is done.
    auto per_thread_local_balancer = cluster_.tls_.get();
    ASSERT(absl::get<std::unique_ptr<AggregateClusterLoadBalancer>>(
               per_thread_local_balancer->lb_) != nullptr);
    auto to_return = std::move(
        absl::get<std::unique_ptr<AggregateClusterLoadBalancer>>(per_thread_local_balancer->lb_));
    per_thread_local_balancer->lb_ = to_return.get();
    return to_return;
  }

  const Cluster& cluster_;
};

// Thread aware load balancer created by the main thread.
struct AggregateThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
  AggregateThreadAwareLoadBalancer(const Cluster& cluster)
      : factory_(std::make_shared<AggregateLoadBalancerFactory>(cluster)) {}

  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  void initialize() override {}

  std::shared_ptr<AggregateLoadBalancerFactory> factory_;
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
