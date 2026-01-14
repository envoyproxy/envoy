#pragma once

#include <memory>
#include <string>

#include "source/extensions/clusters/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

class DynamicModuleClusterConfig;
using DynamicModuleClusterConfigSharedPtr = std::shared_ptr<DynamicModuleClusterConfig>;

/**
 * Function pointer types for the cluster ABI functions.
 */
using OnClusterConfigNewType = decltype(&envoy_dynamic_module_on_cluster_config_new);
using OnClusterConfigDestroyType = decltype(&envoy_dynamic_module_on_cluster_config_destroy);
using OnClusterNewType = decltype(&envoy_dynamic_module_on_cluster_new);
using OnClusterDestroyType = decltype(&envoy_dynamic_module_on_cluster_destroy);
using OnClusterInitType = decltype(&envoy_dynamic_module_on_cluster_init);
using OnClusterCleanupType = decltype(&envoy_dynamic_module_on_cluster_cleanup);
using OnLoadBalancerNewType = decltype(&envoy_dynamic_module_on_load_balancer_new);
using OnLoadBalancerDestroyType = decltype(&envoy_dynamic_module_on_load_balancer_destroy);
using OnLoadBalancerChooseHostType = decltype(&envoy_dynamic_module_on_load_balancer_choose_host);
using OnHostSetChangeType = decltype(&envoy_dynamic_module_on_host_set_change);
using OnHostHealthChangeType = decltype(&envoy_dynamic_module_on_host_health_change);

/**
 * Configuration for a dynamic module cluster. This holds the loaded dynamic module and
 * the resolved function pointers for the ABI.
 */
class DynamicModuleClusterConfig {
public:
  /**
   * Creates a new DynamicModuleClusterConfig.
   *
   * @param cluster_name the name identifying the cluster implementation in the module.
   * @param cluster_config the configuration bytes to pass to the module.
   * @param dynamic_module the loaded dynamic module.
   * @return a shared pointer to the config, or an error status.
   */
  static absl::StatusOr<DynamicModuleClusterConfigSharedPtr>
  newDynamicModuleClusterConfig(const std::string& cluster_name, const std::string& cluster_config,
                                Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleClusterConfig();

  // Function pointers resolved from the dynamic module.
  OnClusterConfigNewType on_cluster_config_new_;
  OnClusterConfigDestroyType on_cluster_config_destroy_;
  OnClusterNewType on_cluster_new_;
  OnClusterDestroyType on_cluster_destroy_;
  OnClusterInitType on_cluster_init_;
  OnClusterCleanupType on_cluster_cleanup_;
  OnLoadBalancerNewType on_load_balancer_new_;
  OnLoadBalancerDestroyType on_load_balancer_destroy_;
  OnLoadBalancerChooseHostType on_load_balancer_choose_host_;
  OnHostSetChangeType on_host_set_change_;
  OnHostHealthChangeType on_host_health_change_;

  // The in-module configuration pointer.
  envoy_dynamic_module_type_cluster_config_module_ptr in_module_config_;

private:
  DynamicModuleClusterConfig(const std::string& cluster_name, const std::string& cluster_config,
                             Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  const std::string cluster_name_;
  const std::string cluster_config_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
