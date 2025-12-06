#pragma once

#include "envoy/server/factory_context.h"

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

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

// Forward declaration.
class DynamicModuleClusterConfig;
using DynamicModuleClusterConfigSharedPtr = std::shared_ptr<DynamicModuleClusterConfig>;

/**
 * Creates a new DynamicModuleClusterConfig for given configuration.
 * @param cluster_name the name of the cluster.
 * @param cluster_config the configuration for the module.
 * @param dynamic_module the dynamic module to use.
 * @param context the server factory context.
 * @return a shared pointer to the new config object or an error if the module could not be loaded.
 */
absl::StatusOr<DynamicModuleClusterConfigSharedPtr>
newDynamicModuleClusterConfig(const absl::string_view cluster_name,
                               const absl::string_view cluster_config,
                               Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                               Server::Configuration::ServerFactoryContext& context);

/**
 * A config to create cluster instances based on a dynamic module. This will be owned by
 * cluster instances. This resolves and holds the symbols used for the cluster.
 */
class DynamicModuleClusterConfig {
public:
  /**
   * Constructor for the config.
   * @param cluster_name the name of the cluster from configuration.
   * @param cluster_config the configuration for the module.
   * @param dynamic_module the dynamic module to use.
   * @param context the server factory context.
   */
  DynamicModuleClusterConfig(const absl::string_view cluster_name,
                             const absl::string_view cluster_config,
                             Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                             Server::Configuration::ServerFactoryContext& context);

  ~DynamicModuleClusterConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_cluster_config_module_ptr in_module_config_ = nullptr;

  // The function pointers for the module related to the cluster. All of them are resolved
  // during the construction of the config and made sure they are not nullptr after that.
  OnClusterConfigDestroyType on_cluster_config_destroy_ = nullptr;
  OnClusterNewType on_cluster_new_ = nullptr;
  OnClusterDestroyType on_cluster_destroy_ = nullptr;
  OnClusterInitType on_cluster_init_ = nullptr;
  OnClusterCleanupType on_cluster_cleanup_ = nullptr;
  OnLoadBalancerNewType on_load_balancer_new_ = nullptr;
  OnLoadBalancerDestroyType on_load_balancer_destroy_ = nullptr;
  OnLoadBalancerChooseHostType on_load_balancer_choose_host_ = nullptr;
  OnHostSetChangeType on_host_set_change_ = nullptr;
  OnHostHealthChangeType on_host_health_change_ = nullptr;

private:
  friend absl::StatusOr<DynamicModuleClusterConfigSharedPtr>
  newDynamicModuleClusterConfig(const absl::string_view cluster_name,
                                 const absl::string_view cluster_config,
                                 Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                 Server::Configuration::ServerFactoryContext& context);

  // The name of the cluster passed in the constructor.
  const std::string cluster_name_;

  // The configuration for the module.
  const std::string cluster_config_;

  // The handle for the module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
