#pragma once

#include <string>

#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace DynamicModules {

// Type aliases for function pointers resolved from the module.
using OnHealthCheckerConfigNewType = decltype(&envoy_dynamic_module_on_health_checker_config_new);
using OnHealthCheckerConfigDestroyType =
    decltype(&envoy_dynamic_module_on_health_checker_config_destroy);
using OnHealthCheckerSessionNewType = decltype(&envoy_dynamic_module_on_health_checker_session_new);
using OnHealthCheckerSessionOnIntervalType =
    decltype(&envoy_dynamic_module_on_health_checker_session_on_interval);
using OnHealthCheckerSessionOnTimeoutType =
    decltype(&envoy_dynamic_module_on_health_checker_session_on_timeout);
using OnHealthCheckerSessionDestroyType =
    decltype(&envoy_dynamic_module_on_health_checker_session_destroy);

/**
 * Configuration for a dynamic module health checker. This resolves and holds the symbols used for
 * health checking. All per-host sessions of one configured custom health checker share this config.
 *
 * Note: Symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleHealthCheckerConfig() to provide graceful error handling. The constructor only
 * initializes basic members.
 */
class DynamicModuleHealthCheckerConfig {
public:
  /**
   * Constructor for the config. Symbol resolution is done in newDynamicModuleHealthCheckerConfig().
   * @param health_checker_name the name of the health checker.
   * @param health_checker_config the configuration bytes for the health checker.
   * @param dynamic_module the dynamic module to use.
   */
  DynamicModuleHealthCheckerConfig(absl::string_view health_checker_name,
                                   absl::string_view health_checker_config,
                                   Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleHealthCheckerConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_health_checker_config_module_ptr in_module_config_{nullptr};

  // The function pointers for the module related to the health checker. All required ones are
  // resolved during newDynamicModuleHealthCheckerConfig() and guaranteed non-nullptr after that.
  OnHealthCheckerConfigDestroyType on_config_destroy_{nullptr};
  OnHealthCheckerSessionNewType on_session_new_{nullptr};
  OnHealthCheckerSessionOnIntervalType on_session_on_interval_{nullptr};
  OnHealthCheckerSessionDestroyType on_session_destroy_{nullptr};
  // Optional timeout callback. Called when the check times out before a result is reported.
  OnHealthCheckerSessionOnTimeoutType on_session_on_timeout_{nullptr};

private:
  // Allow the factory function to access private members for initialization.
  friend absl::StatusOr<std::shared_ptr<DynamicModuleHealthCheckerConfig>>
  newDynamicModuleHealthCheckerConfig(absl::string_view health_checker_name,
                                      absl::string_view health_checker_config,
                                      Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  // The name of the health checker passed in the constructor.
  const std::string health_checker_name_;

  // The configuration bytes for the health checker.
  const std::string health_checker_config_;

  // The handle for the module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleHealthCheckerConfigSharedPtr = std::shared_ptr<DynamicModuleHealthCheckerConfig>;

/**
 * Creates a new DynamicModuleHealthCheckerConfig for the given configuration.
 * @param health_checker_name the name of the health checker.
 * @param health_checker_config the configuration bytes for the health checker.
 * @param dynamic_module the dynamic module to use.
 * @return a shared pointer to the new config object or an error if symbol resolution or in-module
 *         initialization failed.
 */
absl::StatusOr<DynamicModuleHealthCheckerConfigSharedPtr>
newDynamicModuleHealthCheckerConfig(absl::string_view health_checker_name,
                                    absl::string_view health_checker_config,
                                    Extensions::DynamicModules::DynamicModulePtr dynamic_module);

} // namespace DynamicModules
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
