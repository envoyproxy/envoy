#pragma once

#include "envoy/common/callback.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.validate.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/aggregate/lb_context.h"

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

// Order matters so a vector must be used for rebuilds. If the vector size becomes larger we can
// maintain a parallel set for lookups during cluster update callbacks.
using ClusterSet = std::vector<std::string>;
using ClusterSetConstSharedPtr = std::shared_ptr<const ClusterSet>;

class Cluster : public Upstream::ClusterImplBase {
public:
  Cluster(Server::Configuration::ServerFactoryContext& server_context,
          const envoy::config::cluster::v3::Cluster& cluster,
          const envoy::extensions::clusters::aggregate::v3::ClusterConfig& config,
          Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime,
          Random::RandomGenerator& random,
          Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
          Stats::ScopeSharedPtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  Upstream::Cluster::InitializePhase initializePhase() const override {
    return Upstream::Cluster::InitializePhase::Secondary;
  }

  Upstream::ClusterManager& cluster_manager_;
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
  const ClusterSetConstSharedPtr clusters_;

private:
  // Upstream::ClusterImplBase
  void startPreInit() override { onPreInitComplete(); }
};

// Load balancer used by each worker thread. It will be refreshed when clusters, hosts or priorities
// are updated.
class AggregateClusterLoadBalancer : public Upstream::LoadBalancer,
                                     Upstream::ClusterUpdateCallbacks,
                                     Logger::Loggable<Logger::Id::upstream> {
public:
  AggregateClusterLoadBalancer(const Upstream::ClusterInfoConstSharedPtr& parent_info,
                               Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime,
                               Random::RandomGenerator& random,
                               const ClusterSetConstSharedPtr& clusters);

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) override;
  void onClusterRemoval(const std::string& cluster_name) override;

  // Upstream::LoadBalancer
  Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override;
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                           const Upstream::Host& /*host*/,
                           std::vector<uint8_t>& /*hash_key*/) override;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override;

private:
  // Use inner class to extend LoadBalancerBase. When initializing AggregateClusterLoadBalancer, the
  // priority set could be empty, we cannot initialize LoadBalancerBase when priority set is empty.
  class LoadBalancerImpl : public Upstream::LoadBalancerBase {
  public:
    LoadBalancerImpl(const PriorityContext& priority_context, Upstream::ClusterLbStats& lb_stats,
                     Runtime::Loader& runtime, Random::RandomGenerator& random,
                     const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
        : Upstream::LoadBalancerBase(priority_context.priority_set_, lb_stats, runtime, random,
                                     common_config),
          priority_context_(priority_context) {}

    // Upstream::LoadBalancer
    Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;
    // Preconnecting not yet implemented for extensions.
    Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
      return nullptr;
    }
    absl::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                             const Upstream::Host& /*host*/,
                             std::vector<uint8_t>& /*hash_key*/) override {
      return {};
    }
    OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return {};
    }

    absl::optional<uint32_t> hostToLinearizedPriority(const Upstream::HostDescription& host) const;

  private:
    const PriorityContext& priority_context_;
  };

  using LoadBalancerImplPtr = std::unique_ptr<LoadBalancerImpl>;

  void addMemberUpdateCallbackForCluster(Upstream::ThreadLocalCluster& thread_local_cluster);
  PriorityContextPtr linearizePrioritySet(OptRef<const std::string> excluded_cluster);
  void refresh(OptRef<const std::string> excluded_cluster = OptRef<const std::string>());

  LoadBalancerImplPtr load_balancer_;
  Upstream::ClusterInfoConstSharedPtr parent_info_;
  Upstream::ClusterManager& cluster_manager_;
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
  PriorityContextPtr priority_context_;
  const ClusterSetConstSharedPtr clusters_;
  Upstream::ClusterUpdateCallbacksHandlePtr handle_;
  absl::flat_hash_map<std::string, Envoy::Common::CallbackHandlePtr> member_update_cbs_;
};

// Load balancer factory created by the main thread and will be called in each worker thread to
// create the thread local load balancer.
struct AggregateLoadBalancerFactory : public Upstream::LoadBalancerFactory {
  AggregateLoadBalancerFactory(const Cluster& cluster) : cluster_(cluster) {}
  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create() override {
    return std::make_unique<AggregateClusterLoadBalancer>(
        cluster_.info(), cluster_.cluster_manager_, cluster_.runtime_, cluster_.random_,
        cluster_.clusters_);
  }
  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override { return create(); }

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
  ClusterFactory() : ConfigurableClusterFactoryBase("envoy.clusters.aggregate") {}

private:
  std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
  createClusterWithConfig(
      Server::Configuration::ServerFactoryContext& server_context,
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::aggregate::v3::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopeSharedPtr&& stats_scope) override;
};

DECLARE_FACTORY(ClusterFactory);

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
