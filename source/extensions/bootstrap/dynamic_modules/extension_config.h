#pragma once

#include <string>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

using OnBootstrapExtensionConfigDestroyType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_config_destroy);
using OnBootstrapExtensionNewType = decltype(&envoy_dynamic_module_on_bootstrap_extension_new);
using OnBootstrapExtensionServerInitializedType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_server_initialized);
using OnBootstrapExtensionWorkerThreadInitializedType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized);
using OnBootstrapExtensionDestroyType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_destroy);

class DynamicModuleBootstrapExtension;

/**
 * A config to create bootstrap extensions based on a dynamic module. This will be owned by the
 * bootstrap extension. This resolves and holds the symbols used for the bootstrap extension.
 */
class DynamicModuleBootstrapExtensionConfig {
public:
  /**
   * Constructor for the config.
   * @param extension_name the name of the extension.
   * @param extension_config the configuration for the module.
   * @param dynamic_module the dynamic module to use.
   */
  DynamicModuleBootstrapExtensionConfig(
      const absl::string_view extension_name, const absl::string_view extension_config,
      Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleBootstrapExtensionConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_bootstrap_extension_config_module_ptr in_module_config_ = nullptr;

  // The function pointers for the module related to the bootstrap extension. All of them are
  // resolved during the construction of the config and made sure they are not nullptr after that.

  OnBootstrapExtensionConfigDestroyType on_bootstrap_extension_config_destroy_ = nullptr;
  OnBootstrapExtensionNewType on_bootstrap_extension_new_ = nullptr;
  OnBootstrapExtensionServerInitializedType on_bootstrap_extension_server_initialized_ = nullptr;
  OnBootstrapExtensionWorkerThreadInitializedType
      on_bootstrap_extension_worker_thread_initialized_ = nullptr;
  OnBootstrapExtensionDestroyType on_bootstrap_extension_destroy_ = nullptr;

  // The dynamic module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleBootstrapExtensionConfigSharedPtr =
    std::shared_ptr<DynamicModuleBootstrapExtensionConfig>;

/**
 * Creates a new DynamicModuleBootstrapExtensionConfig from the given module and configuration.
 * @param extension_name the name of the extension.
 * @param extension_config the configuration for the module.
 * @param dynamic_module the dynamic module to use.
 * @return an error status if the module could not be loaded or the configuration could not be
 * created, or a shared pointer to the config.
 */
absl::StatusOr<DynamicModuleBootstrapExtensionConfigSharedPtr>
newDynamicModuleBootstrapExtensionConfig(
    const absl::string_view extension_name, const absl::string_view extension_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module);

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
