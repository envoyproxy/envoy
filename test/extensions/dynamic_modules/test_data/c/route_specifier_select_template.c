#include "source/extensions/dynamic_modules/abi/abi.h"

// This module always selects the route template named "only", so that a test can drive the route
// of a request entirely from the module.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_route_specifier_config_module_ptr
envoy_dynamic_module_on_route_specifier_config_new(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_route_specifier_config_destroy(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr) {}

envoy_dynamic_module_type_route_specifier_decision envoy_dynamic_module_on_route_specifier_on_route(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr) {
  const char* template_id = "only";
  envoy_dynamic_module_type_module_buffer id = {template_id, 4};
  if (!envoy_dynamic_module_callback_route_specifier_set_template(context_envoy_ptr, id)) {
    return envoy_dynamic_module_type_route_specifier_decision_Error;
  }
  return envoy_dynamic_module_type_route_specifier_decision_SelectTemplate;
}
