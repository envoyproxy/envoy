// NOLINT(namespace-envoy)

// This file provides host-side implementations for ABI callbacks specific to bootstrap extensions.

#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"
#include "source/extensions/dynamic_modules/abi.h"

using Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtensionConfig;
using Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtensionConfigScheduler;

extern "C" {

envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr
envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(extension_config_envoy_ptr);
  return new DynamicModuleBootstrapExtensionConfigScheduler(config->weak_from_this(),
                                                            config->main_thread_dispatcher_);
}

void envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(
    envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr
        scheduler_module_ptr) {
  delete static_cast<DynamicModuleBootstrapExtensionConfigScheduler*>(scheduler_module_ptr);
}

void envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(
    envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id) {
  auto* scheduler =
      static_cast<DynamicModuleBootstrapExtensionConfigScheduler*>(scheduler_module_ptr);
  scheduler->commit(event_id);
}

} // extern "C"
