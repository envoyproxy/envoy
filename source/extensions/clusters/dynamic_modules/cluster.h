#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.validate.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

class DynamicModuleCluster;
class DynamicModuleClusterTestPeer;

// Function pointer types for the cluster ABI event hooks.
using OnClusterConfigNewType = decltype(&envoy_dynamic_module_on_cluster_config_new);
using OnClusterConfigDestroyType = decltype(&envoy_dynamic_module_on_cluster_config_destroy);
using OnClusterNewType = decltype(&envoy_dynamic_module_on_cluster_new);
using OnClusterInitType = decltype(&envoy_dynamic_module_on_cluster_init);
using OnClusterDestroyType = decltype(&envoy_dynamic_module_on_cluster_destroy);
using OnClusterLbNewType = decltype(&envoy_dynamic_module_on_cluster_lb_new);
using OnClusterLbDestroyType = decltype(&envoy_dynamic_module_on_cluster_lb_destroy);
using OnClusterLbChooseHostType = decltype(&envoy_dynamic_module_on_cluster_lb_choose_host);

/**
 * Configuration for a dynamic module cluster. This holds the loaded dynamic module, resolved
 * function pointers, and the in-module configuration.
 */
class DynamicModuleClusterConfig {
public:
  /**
   * Creates a new DynamicModuleClusterConfig.
   *
   * @param cluster_name the name identifying the cluster implementation in the module.
   * @param cluster_config the configuration bytes to pass to the module.
   * @param module the loaded dynamic module.
   * @return a shared pointer to the config, or an error status.
   */
  static absl::StatusOr<std::shared_ptr<DynamicModuleClusterConfig>>
  create(const std::string& cluster_name, const std::string& cluster_config,
         Envoy::Extensions::DynamicModules::DynamicModulePtr module);

  ~DynamicModuleClusterConfig();

  // Function pointers resolved from the dynamic module.
  OnClusterConfigNewType on_cluster_config_new_ = nullptr;
  OnClusterConfigDestroyType on_cluster_config_destroy_ = nullptr;
  OnClusterNewType on_cluster_new_ = nullptr;
  OnClusterInitType on_cluster_init_ = nullptr;
  OnClusterDestroyType on_cluster_destroy_ = nullptr;
  OnClusterLbNewType on_cluster_lb_new_ = nullptr;
  OnClusterLbDestroyType on_cluster_lb_destroy_ = nullptr;
  OnClusterLbChooseHostType on_cluster_lb_choose_host_ = nullptr;

  // The in-module configuration pointer.
  envoy_dynamic_module_type_cluster_config_module_ptr in_module_config_ = nullptr;

private:
  DynamicModuleClusterConfig(const std::string& cluster_name, const std::string& cluster_config,
                             Envoy::Extensions::DynamicModules::DynamicModulePtr module);

  const std::string cluster_name_;
  const std::string cluster_config_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleClusterConfigSharedPtr = std::shared_ptr<DynamicModuleClusterConfig>;

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
 * The DynamicModuleCluster delegates host discovery and load balancing to a dynamic module.
 * The module manages hosts via add/remove callbacks and provides its own load balancer.
 */
class DynamicModuleCluster : public Upstream::ClusterImplBase {
public:
  ~DynamicModuleCluster() override;

  // Upstream::Cluster
  Upstream::Cluster::InitializePhase initializePhase() const override {
    return Upstream::Cluster::InitializePhase::Primary;
  }

  // Methods called by the dynamic module via ABI callbacks.
  bool addHosts(const std::vector<std::string>& addresses, const std::vector<uint32_t>& weights,
                std::vector<Upstream::HostSharedPtr>& result_hosts);
  size_t removeHosts(const std::vector<Upstream::HostSharedPtr>& hosts);
  Upstream::HostSharedPtr findHost(void* raw_host_ptr);
  void preInitComplete();

  // Accessors.
  const DynamicModuleClusterConfigSharedPtr& config() const { return config_; }
  envoy_dynamic_module_type_cluster_module_ptr inModuleCluster() const {
    return in_module_cluster_;
  }

protected:
  DynamicModuleCluster(const envoy::config::cluster::v3::Cluster& cluster,
                       DynamicModuleClusterConfigSharedPtr config,
                       Upstream::ClusterFactoryContext& context, absl::Status& creation_status);

  // Upstream::ClusterImplBase.
  void startPreInit() override;

private:
  friend class DynamicModuleClusterFactory;
  friend class DynamicModuleClusterTestPeer;
  friend class DynamicModuleClusterHandle;
  friend class DynamicModuleLoadBalancer;

  DynamicModuleClusterConfigSharedPtr config_;
  envoy_dynamic_module_type_cluster_module_ptr in_module_cluster_;
  Event::Dispatcher& dispatcher_;

  // Map from raw host pointer to shared pointer for lookup in chooseHost.
  absl::Mutex host_map_lock_;
  absl::flat_hash_map<void*, Upstream::HostSharedPtr> host_map_ ABSL_GUARDED_BY(host_map_lock_);
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

  // Access the priority set for lb callbacks.
  const Upstream::PrioritySet& prioritySet() const;

private:
  const DynamicModuleClusterHandleSharedPtr handle_;
  envoy_dynamic_module_type_cluster_lb_module_ptr in_module_lb_;
};

/**
 * Factory for creating DynamicModuleCluster instances.
 */
class DynamicModuleClusterFactory
    : public Upstream::ConfigurableClusterFactoryBase<
          envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig> {
public:
  DynamicModuleClusterFactory()
      : ConfigurableClusterFactoryBase("envoy.clusters.dynamic_modules") {}

private:
  friend class DynamicModuleClusterFactoryTestPeer;
  absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(DynamicModuleClusterFactory);

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
