#include <stddef.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// A route extension module whose config new hook fails.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_route_extension_config_module_ptr
envoy_dynamic_module_on_route_extension_config_new(
    envoy_dynamic_module_type_route_extension_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  // Return nullptr to simulate initialization failure.
  return NULL;
}

void envoy_dynamic_module_on_route_extension_config_destroy(
    envoy_dynamic_module_type_route_extension_config_module_ptr config_module_ptr) {}

envoy_dynamic_module_type_route_extension_decision envoy_dynamic_module_on_route_extension_on_route(
    envoy_dynamic_module_type_route_extension_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_route_extension_context_envoy_ptr context_envoy_ptr) {
  return envoy_dynamic_module_type_route_extension_decision_Keep;
}
