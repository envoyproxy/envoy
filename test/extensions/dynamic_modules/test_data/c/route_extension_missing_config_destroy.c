#include "source/extensions/dynamic_modules/abi/abi.h"

// A route extension module that is missing the config destroy hook.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_route_extension_config_module_ptr
envoy_dynamic_module_on_route_extension_config_new(
    envoy_dynamic_module_type_route_extension_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  static int config_dummy = 0;
  return &config_dummy;
}
