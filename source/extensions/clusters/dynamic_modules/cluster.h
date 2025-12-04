#pragma once

#include <memory>

#include "envoy/common/callback.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/dynamic_modules/cluster_config.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

class DynamicModuleCluster;
class DynamicModuleLoadBalancer;

using DynamicModuleClusterSharedPtr = std::shared_ptr<DynamicModuleCluster>;

/**
 * Implementation of Upstream::Cluster for dynamic module clusters.
 * This cluster delegates host management and load balancing to the dynamic module.
 */
class DynamicModuleCluster : public Upstream::ClusterImplBase,
                             public std::enable_shared_from_this<DynamicModuleCluster> {
public:
  DynamicModuleCluster(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig& config,
      DynamicModuleClusterConfigSharedPtr module_config, Upstream::ClusterFactoryContext& context,
      absl::Status& creation_status);
  ~DynamicModuleCluster() override;

  // Upstream::Cluster
  Upstream::Cluster::InitializePhase initializePhase() const override {
    return Upstream::Cluster::InitializePhase::Primary;
  }

  /**
   * Get the module configuration.
   */
  const DynamicModuleClusterConfig& config() const { return *config_; }

  /**
   * Get the module cluster pointer.
   */
  envoy_dynamic_module_type_cluster_module_ptr moduleClusterPtr() const {
    return in_module_cluster_;
  }

  /**
   * Get this cluster as a void pointer for passing to the module.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

  /**
   * Add a new host to the cluster dynamically.
   * @param address the IP address string.
   * @param port the port number.
   * @param weight the load balancing weight (1-128).
   * @return the created host, or an error.
   */
  absl::StatusOr<Upstream::HostSharedPtr> addHost(const std::string& address, uint32_t port,
                                                  uint32_t weight);

  /**
   * Remove a host from the cluster.
   * @param host the host to remove.
   */
  void removeHost(const Upstream::HostSharedPtr& host);

  /**
   * Get a host by address and port.
   * @param address the IP address string.
   * @param port the port number.
   * @return the host, or nullptr if not found.
   */
  Upstream::HostConstSharedPtr getHostByAddress(const std::string& address, uint32_t port) const;

  /**
   * Get all current hosts.
   * @return a vector of host information.
   */
  std::vector<std::pair<Upstream::HostSharedPtr, envoy_dynamic_module_type_host_info>>
  getHosts() const;

  /**
   * Signal that pre-initialization is complete.
   * This is called by the module via the callback.
   */
  void onPreInitComplete() { ClusterImplBase::onPreInitComplete(); }

  /**
   * Get the dispatcher for scheduling cleanup.
   */
  Event::Dispatcher& dispatcher() { return dispatcher_; }

  /**
   * Get the cluster manager for cross-cluster access.
   */
  Upstream::ClusterManager& clusterManager() { return cluster_manager_; }

protected:
  // ClusterImplBase
  void startPreInit() override;

private:
  friend class DynamicModuleClusterFactory;

  /**
   * Cleanup callback for periodic maintenance.
   */
  void cleanup();

  /**
   * Build a host key for the map.
   */
  std::string buildHostKey(const std::string& address, uint32_t port) const;

  /**
   * Handle host set changes from the priority set.
   */
  void onHostSetChange(const Upstream::HostVector& hosts_added,
                       const Upstream::HostVector& hosts_removed);

  /**
   * Handle health check completion for a host.
   */
  void onHealthCheckComplete(Upstream::HostSharedPtr host, Upstream::HealthTransition transition,
                             Upstream::HealthState current_health);

  /**
   * Thread-aware load balancer that creates per-worker load balancers.
   */
  class ThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
  public:
    ThreadAwareLoadBalancer(DynamicModuleClusterSharedPtr cluster) : cluster_(cluster) {}

    // Upstream::ThreadAwareLoadBalancer
    Upstream::LoadBalancerFactorySharedPtr factory() override;
    absl::Status initialize() override { return absl::OkStatus(); }

  private:
    DynamicModuleClusterSharedPtr cluster_;
  };

  DynamicModuleClusterConfigSharedPtr config_;
  envoy_dynamic_module_type_cluster_module_ptr in_module_cluster_ = nullptr;

  Event::Dispatcher& dispatcher_;
  Upstream::ClusterManager& cluster_manager_;
  Event::TimerPtr cleanup_timer_;
  const std::chrono::milliseconds cleanup_interval_;
  const uint32_t max_hosts_;

  mutable absl::Mutex host_map_lock_;
  absl::flat_hash_map<std::string, Upstream::HostSharedPtr>
      host_map_ ABSL_GUARDED_BY(host_map_lock_);

  // Callback handles for cleanup.
  Envoy::Common::CallbackHandlePtr member_update_cb_handle_;
};

/**
 * Load balancer for dynamic module clusters.
 * This load balancer delegates host selection to the dynamic module.
 */
class DynamicModuleLoadBalancer : public Upstream::LoadBalancer {
public:
  DynamicModuleLoadBalancer(DynamicModuleClusterSharedPtr cluster);
  ~DynamicModuleLoadBalancer() override;

  // Upstream::LoadBalancer
  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
    return nullptr;
  }
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext*, const Upstream::Host&,
                           std::vector<uint8_t>&) override {
    return absl::nullopt;
  }
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
    return {};
  }

private:
  DynamicModuleClusterSharedPtr cluster_;
  envoy_dynamic_module_type_load_balancer_module_ptr in_module_lb_ = nullptr;
};

/**
 * Factory for creating dynamic module load balancers.
 */
class DynamicModuleLoadBalancerFactory : public Upstream::LoadBalancerFactory {
public:
  DynamicModuleLoadBalancerFactory(DynamicModuleClusterSharedPtr cluster) : cluster_(cluster) {}

  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override {
    return std::make_unique<DynamicModuleLoadBalancer>(cluster_);
  }

private:
  DynamicModuleClusterSharedPtr cluster_;
};

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
