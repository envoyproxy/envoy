#include "source/extensions/dynamic_modules/abi/abi.h"

// This module is missing envoy_dynamic_module_on_route_provider_config_destroy.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_route_provider_config_module_ptr
envoy_dynamic_module_on_route_provider_config_new(
    envoy_dynamic_module_type_route_provider_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config,
    size_t route_template_count) {
  static int config_dummy = 0;
  return &config_dummy;
}
