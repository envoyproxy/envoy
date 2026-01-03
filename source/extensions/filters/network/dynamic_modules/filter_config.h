#pragma once

#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace NetworkFilters {

using OnNetworkConfigDestroyType = decltype(&envoy_dynamic_module_on_network_filter_config_destroy);
using OnNetworkFilterNewType = decltype(&envoy_dynamic_module_on_network_filter_new);
using OnNetworkFilterNewConnectionType =
    decltype(&envoy_dynamic_module_on_network_filter_new_connection);
using OnNetworkFilterReadType = decltype(&envoy_dynamic_module_on_network_filter_read);
using OnNetworkFilterWriteType = decltype(&envoy_dynamic_module_on_network_filter_write);
using OnNetworkFilterEventType = decltype(&envoy_dynamic_module_on_network_filter_event);
using OnNetworkFilterDestroyType = decltype(&envoy_dynamic_module_on_network_filter_destroy);
using OnNetworkFilterHttpCalloutDoneType =
    decltype(&envoy_dynamic_module_on_network_filter_http_callout_done);

/**
 * A config to create network filters based on a dynamic module. This will be owned by multiple
 * filter instances. This resolves and holds the symbols used for the network filters.
 * Each filter instance and the factory callback holds a shared pointer to this config.
 *
 * Note: Symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleNetworkFilterConfig() to provide graceful error handling. The constructor
 * only initializes basic members.
 */
class DynamicModuleNetworkFilterConfig {
public:
  /**
   * Constructor for the config. Symbol resolution is done in newDynamicModuleNetworkFilterConfig().
   * @param filter_name the name of the filter.
   * @param filter_config the configuration for the module.
   * @param dynamic_module the dynamic module to use.
   * @param cluster_manager the cluster manager for async HTTP callouts.
   */
  DynamicModuleNetworkFilterConfig(const absl::string_view filter_name,
                                   const absl::string_view filter_config,
                                   DynamicModulePtr dynamic_module,
                                   Envoy::Upstream::ClusterManager& cluster_manager);

  ~DynamicModuleNetworkFilterConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_network_filter_config_module_ptr in_module_config_ = nullptr;

  // The function pointers for the module related to the network filter. All of them are resolved
  // during newDynamicModuleNetworkFilterConfig() and made sure they are not nullptr after that.

  OnNetworkConfigDestroyType on_network_filter_config_destroy_ = nullptr;
  OnNetworkFilterNewType on_network_filter_new_ = nullptr;
  OnNetworkFilterNewConnectionType on_network_filter_new_connection_ = nullptr;
  OnNetworkFilterReadType on_network_filter_read_ = nullptr;
  OnNetworkFilterWriteType on_network_filter_write_ = nullptr;
  OnNetworkFilterEventType on_network_filter_event_ = nullptr;
  OnNetworkFilterDestroyType on_network_filter_destroy_ = nullptr;
  OnNetworkFilterHttpCalloutDoneType on_network_filter_http_callout_done_ = nullptr;

  Envoy::Upstream::ClusterManager& cluster_manager_;

private:
  // Allow the factory function to access private members for initialization.
  friend absl::StatusOr<std::shared_ptr<DynamicModuleNetworkFilterConfig>>
  newDynamicModuleNetworkFilterConfig(const absl::string_view filter_name,
                                      const absl::string_view filter_config,
                                      DynamicModulePtr dynamic_module,
                                      Envoy::Upstream::ClusterManager& cluster_manager);

  // The name of the filter passed in the constructor.
  const std::string filter_name_;

  // The configuration for the module.
  const std::string filter_config_;

  // The handle for the module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleNetworkFilterConfigSharedPtr = std::shared_ptr<DynamicModuleNetworkFilterConfig>;

/**
 * Creates a new DynamicModuleNetworkFilterConfig for given configuration.
 * @param filter_name the name of the filter.
 * @param filter_config the configuration for the module.
 * @param dynamic_module the dynamic module to use.
 * @param cluster_manager the cluster manager for async HTTP callouts.
 * @return a shared pointer to the new config object or an error if the module could not be loaded.
 */
absl::StatusOr<DynamicModuleNetworkFilterConfigSharedPtr>
newDynamicModuleNetworkFilterConfig(const absl::string_view filter_name,
                                    const absl::string_view filter_config,
                                    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                    Envoy::Upstream::ClusterManager& cluster_manager);

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
