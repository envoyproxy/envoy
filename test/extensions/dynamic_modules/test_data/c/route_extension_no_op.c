#include "source/extensions/dynamic_modules/abi/abi.h"

// This is a minimal implementation of a route extension module.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_route_extension_config_module_ptr
envoy_dynamic_module_on_route_extension_config_new(
    envoy_dynamic_module_type_route_extension_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  // Return a dummy pointer.
  static int config_dummy = 0;
  return &config_dummy;
}

static int config_destroy_count = 0;

// Lets tests observe that Envoy invoked the config destroy hook.
int getConfigDestroyCount(void) { return config_destroy_count; }

void envoy_dynamic_module_on_route_extension_config_destroy(
    envoy_dynamic_module_type_route_extension_config_module_ptr config_module_ptr) {
  config_destroy_count++;
}

envoy_dynamic_module_type_route_extension_decision envoy_dynamic_module_on_route_extension_on_route(
    envoy_dynamic_module_type_route_extension_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_route_extension_context_envoy_ptr context_envoy_ptr) {
  // Keep the route unchanged.
  return envoy_dynamic_module_type_route_extension_decision_Keep;
}
