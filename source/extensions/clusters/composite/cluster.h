#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.validate.h"
#include "envoy/http/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/composite/lb_context.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {

/**
 * Implementation of Upstream::Cluster for Composite cluster.
 *
 * This cluster type provides flexible sub-cluster selection strategies for various use cases
 * including retry progression, cross-cluster failover, and potential future support for
 * stateful session affinity.
 */
class Cluster : public Upstream::ClusterImplBase {
public:
  Cluster(const envoy::config::cluster::v3::Cluster& cluster,
          const envoy::extensions::clusters::composite::v3::CompositeCluster& config,
          Upstream::ClusterFactoryContext& context, absl::Status& creation_status);

  // Upstream::Cluster
  Upstream::Cluster::InitializePhase initializePhase() const override {
    return Upstream::Cluster::InitializePhase::Primary;
  }

  // Getters for testing and load balancer access.
  Runtime::Loader& runtime() { return runtime_; }
  Random::RandomGenerator& random() { return random_; }

  // Access to configuration for load balancer.
  const std::vector<std::string>* subClusters() const { return sub_clusters_.get(); }
  envoy::extensions::clusters::composite::v3::CompositeCluster::ClusterMode mode() const {
    return mode_;
  }
  const envoy::extensions::clusters::composite::v3::CompositeCluster::RetryConfig&
  retryConfig() const {
    return retry_config_;
  }
  const std::string& name() const { return name_; }
  bool honorRouteRetryPolicy() const { return honor_route_retry_policy_; }
  Upstream::ClusterFactoryContext& context() { return context_; }
  const Upstream::ClusterFactoryContext& context() const { return context_; }

private:
  void startPreInit() override { onPreInitComplete(); }

  Upstream::ClusterFactoryContext& context_;
  std::unique_ptr<std::vector<std::string>> sub_clusters_;
  envoy::extensions::clusters::composite::v3::CompositeCluster::ClusterMode mode_;
  envoy::extensions::clusters::composite::v3::CompositeCluster::RetryConfig retry_config_;
  std::string name_;
  bool honor_route_retry_policy_;
};

/**
 * Connection lifetime callbacks aggregator for composite cluster.
 * Manages callbacks from all sub-clusters to provide a unified interface.
 */
class CompositeConnectionLifetimeCallbacks
    : public Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks {
public:
  CompositeConnectionLifetimeCallbacks() = default;

  // Http::ConnectionPool::ConnectionLifetimeCallbacks
  void onConnectionOpen(Envoy::Http::ConnectionPool::Instance& pool, std::vector<uint8_t>& hash_key,
                        const Network::Connection& connection) override;
  void onConnectionDraining(Envoy::Http::ConnectionPool::Instance& pool,
                            std::vector<uint8_t>& hash_key,
                            const Network::Connection& connection) override;

  void addCallback(Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks* callback);
  void removeCallback(Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks* callback);
  void clearCallbacks();

private:
  std::vector<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks*> callbacks_;
};

/**
 * Load balancer implementation for Composite cluster.
 *
 * Implements the cluster selection logic based on retry attempts and configured mode.
 * Currently supports ``RETRY`` mode with various overflow handling options.
 */
class CompositeClusterLoadBalancer : public Upstream::LoadBalancer,
                                     public Upstream::ClusterUpdateCallbacks,
                                     protected Logger::Loggable<Logger::Id::upstream> {
public:
  CompositeClusterLoadBalancer(
      const Upstream::ClusterInfo& cluster_info, Upstream::ClusterManager& cluster_manager,
      const std::vector<std::string>* sub_clusters,
      envoy::extensions::clusters::composite::v3::CompositeCluster::ClusterMode mode,
      const envoy::extensions::clusters::composite::v3::CompositeCluster::RetryConfig& retry_config,
      bool honor_route_retry_policy);

  // Upstream::LoadBalancer
  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext* context) override;
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext* context, const Upstream::Host& host,
                           std::vector<uint8_t>& hash_key) override;

  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
    return makeOptRef(*composite_callbacks_);
  }

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(absl::string_view cluster_name,
                            Upstream::ThreadLocalClusterCommand& get_cluster) override;
  void onClusterRemoval(const std::string& cluster_name) override;

  uint32_t getRetryAttemptCount(Upstream::LoadBalancerContext* context) const;
  absl::optional<size_t> mapRetryAttemptToClusterIndex(size_t retry_attempt) const;
  Upstream::ThreadLocalCluster* getClusterByIndex(size_t cluster_index) const;

private:
  void addMemberUpdateCallbackForCluster(Upstream::ThreadLocalCluster& cluster);

  const Upstream::ClusterInfo& cluster_info_;
  Upstream::ClusterManager& cluster_manager_;
  const std::vector<std::string>* sub_clusters_;
  envoy::extensions::clusters::composite::v3::CompositeCluster::ClusterMode mode_;
  envoy::extensions::clusters::composite::v3::CompositeCluster::RetryConfig retry_config_;
  bool honor_route_retry_policy_;
  std::unique_ptr<CompositeConnectionLifetimeCallbacks> composite_callbacks_;

  // Member update callback handles for cleanup.
  std::vector<Envoy::Common::CallbackHandlePtr> member_update_cbs_;
};

/**
 * Load balancer factory for Composite cluster.
 */
class CompositeClusterLoadBalancerFactory : public Upstream::LoadBalancerFactory {
public:
  CompositeClusterLoadBalancerFactory(
      const Upstream::ClusterInfo& cluster_info, Upstream::ClusterManager& cluster_manager,
      const std::vector<std::string>* sub_clusters,
      envoy::extensions::clusters::composite::v3::CompositeCluster::ClusterMode mode,
      const envoy::extensions::clusters::composite::v3::CompositeCluster::RetryConfig& retry_config,
      bool honor_route_retry_policy)
      : cluster_info_(cluster_info), cluster_manager_(cluster_manager), sub_clusters_(sub_clusters),
        mode_(mode), retry_config_(retry_config),
        honor_route_retry_policy_(honor_route_retry_policy) {}

  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams /*params*/) override {
    return std::make_unique<CompositeClusterLoadBalancer>(cluster_info_, cluster_manager_,
                                                          sub_clusters_, mode_, retry_config_,
                                                          honor_route_retry_policy_);
  }

private:
  const Upstream::ClusterInfo& cluster_info_;
  Upstream::ClusterManager& cluster_manager_;
  const std::vector<std::string>* sub_clusters_;
  envoy::extensions::clusters::composite::v3::CompositeCluster::ClusterMode mode_;
  envoy::extensions::clusters::composite::v3::CompositeCluster::RetryConfig retry_config_;
  bool honor_route_retry_policy_;
};

// Thread aware load balancer created by the main thread.
struct CompositeThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
  CompositeThreadAwareLoadBalancer(const Cluster& cluster)
      : factory_(std::make_shared<CompositeClusterLoadBalancerFactory>(
            *cluster.info(),
            const_cast<Cluster&>(cluster).context().serverFactoryContext().clusterManager(),
            cluster.subClusters(), cluster.mode(), cluster.retryConfig(),
            cluster.honorRouteRetryPolicy())) {}

  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override { return absl::OkStatus(); }

  std::shared_ptr<CompositeClusterLoadBalancerFactory> factory_;
};

/**
 * Factory for creating Composite clusters.
 */
class ClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                           envoy::extensions::clusters::composite::v3::CompositeCluster> {
public:
  ClusterFactory() : ConfigurableClusterFactoryBase("envoy.clusters.composite") {}

private:
  absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::composite::v3::CompositeCluster& proto_config,
      Upstream::ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(ClusterFactory);

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
