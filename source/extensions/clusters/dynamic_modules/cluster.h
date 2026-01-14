#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/dynamic_modules/abi.h"
#include "source/extensions/clusters/dynamic_modules/cluster_config.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

class DynamicModuleCluster;
class DynamicModuleClusterTestPeer;

/**
 * Handle object to ensure that the destructor of DynamicModuleCluster is called on the main
 * thread.
 */
class DynamicModuleClusterHandle {
public:
  DynamicModuleClusterHandle(std::shared_ptr<DynamicModuleCluster> cluster)
      : cluster_(std::move(cluster)) {}
  ~DynamicModuleClusterHandle();

private:
  std::shared_ptr<DynamicModuleCluster> cluster_;
  friend class DynamicModuleCluster;
  friend class DynamicModuleLoadBalancer;
};

using DynamicModuleClusterHandleSharedPtr = std::shared_ptr<DynamicModuleClusterHandle>;

/**
 * The DynamicModuleCluster is a cluster that delegates its behavior to a dynamic module.
 */
class DynamicModuleCluster : public Upstream::ClusterImplBase {
public:
  ~DynamicModuleCluster() override;

  // Upstream::Cluster
  Upstream::Cluster::InitializePhase initializePhase() const override {
    return Upstream::Cluster::InitializePhase::Primary;
  }

  // Methods called by the dynamic module via ABI callbacks.
  envoy_dynamic_module_type_cluster_error addHost(const std::string& address, uint32_t weight,
                                                  Upstream::HostSharedPtr* result_host);
  envoy_dynamic_module_type_cluster_error removeHost(Upstream::HostSharedPtr host);
  std::vector<envoy_dynamic_module_type_host_info> getHosts();
  Upstream::HostSharedPtr getHostByAddress(const std::string& address);
  envoy_dynamic_module_type_cluster_error setHostWeight(Upstream::HostSharedPtr host,
                                                        uint32_t weight);
  void preInitComplete();

  // Accessors.
  const DynamicModuleClusterConfigSharedPtr& config() const { return config_; }
  envoy_dynamic_module_type_cluster_module_ptr inModuleCluster() const {
    return in_module_cluster_;
  }
  Upstream::ClusterManager& clusterManager() const { return cluster_manager_; }

protected:
  DynamicModuleCluster(const envoy::config::cluster::v3::Cluster& cluster,
                       DynamicModuleClusterConfigSharedPtr config, bool dynamic_host_discovery,
                       std::chrono::milliseconds cleanup_interval, uint32_t max_hosts,
                       Upstream::ClusterFactoryContext& context, absl::Status& creation_status);

  // Upstream::ClusterImplBase.
  void startPreInit() override;

private:
  friend class DynamicModuleClusterFactory;
  friend class DynamicModuleClusterTestPeer;
  friend class DynamicModuleClusterHandle;

  void cleanup();
  void onHostSetChange(const Upstream::HostVector& hosts_added,
                       const Upstream::HostVector& hosts_removed);
  void onHealthCheckComplete(Upstream::HostSharedPtr host,
                             Upstream::HealthTransition health_transition);

  static envoy_dynamic_module_type_host_health convertHealth(Upstream::Host::Health health);
  static envoy_dynamic_module_type_health_transition
  convertHealthTransition(Upstream::HealthTransition transition);

  DynamicModuleClusterConfigSharedPtr config_;
  envoy_dynamic_module_type_cluster_module_ptr in_module_cluster_;
  Event::Dispatcher& dispatcher_;
  Upstream::ClusterManager& cluster_manager_;
  const bool dynamic_host_discovery_;
  const std::chrono::milliseconds cleanup_interval_;
  const uint32_t max_hosts_;
  Event::TimerPtr cleanup_timer_;

  // Map from address string to host for quick lookup.
  absl::Mutex host_map_lock_;
  absl::flat_hash_map<std::string, Upstream::HostSharedPtr>
      host_map_ ABSL_GUARDED_BY(host_map_lock_);

  // Callback for member updates.
  Envoy::Common::CallbackHandlePtr member_update_cb_;
};

/**
 * Load balancer that delegates to the dynamic module.
 */
class DynamicModuleLoadBalancer : public Upstream::LoadBalancer {
public:
  DynamicModuleLoadBalancer(const DynamicModuleClusterHandleSharedPtr& handle);
  ~DynamicModuleLoadBalancer() override;

  // Upstream::LoadBalancer.
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
  const DynamicModuleClusterHandleSharedPtr handle_;
  envoy_dynamic_module_type_load_balancer_module_ptr in_module_lb_;
};

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
