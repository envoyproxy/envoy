#pragma once

#include <memory>
#include <string>

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {

class DynamicModuleLbConfig;
using DynamicModuleLbConfigSharedPtr = std::shared_ptr<DynamicModuleLbConfig>;

/**
 * Function pointer types for the load balancer ABI functions.
 */
using OnLbConfigNewType = decltype(&envoy_dynamic_module_on_lb_config_new);
using OnLbConfigDestroyType = decltype(&envoy_dynamic_module_on_lb_config_destroy);
using OnLbNewType = decltype(&envoy_dynamic_module_on_lb_new);
using OnLbChooseHostType = decltype(&envoy_dynamic_module_on_lb_choose_host);
using OnLbDestroyType = decltype(&envoy_dynamic_module_on_lb_destroy);

/**
 * Configuration for a dynamic module load balancer. This holds the loaded dynamic module and
 * the resolved function pointers for the ABI.
 */
class DynamicModuleLbConfig : public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  /**
   * Creates a new DynamicModuleLbConfig.
   *
   * @param lb_policy_name the name identifying the load balancer implementation in the module.
   * @param lb_config the configuration bytes to pass to the module.
   * @param dynamic_module the loaded dynamic module.
   * @return a shared pointer to the config, or an error status.
   */
  static absl::StatusOr<DynamicModuleLbConfigSharedPtr>
  create(const std::string& lb_policy_name, const std::string& lb_config,
         Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleLbConfig();

  // Function pointers resolved from the dynamic module.
  OnLbConfigNewType on_config_new_;
  OnLbConfigDestroyType on_config_destroy_;
  OnLbNewType on_lb_new_;
  OnLbChooseHostType on_choose_host_;
  OnLbDestroyType on_lb_destroy_;

  // The in-module configuration pointer.
  envoy_dynamic_module_type_lb_config_module_ptr in_module_config_;

private:
  DynamicModuleLbConfig(const std::string& lb_policy_name, const std::string& lb_config,
                        Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  const std::string lb_policy_name_;
  const std::string lb_config_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
