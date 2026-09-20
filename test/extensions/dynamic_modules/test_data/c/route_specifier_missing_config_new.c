#include "source/extensions/dynamic_modules/abi/abi.h"

// This module is missing the config new function.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

void envoy_dynamic_module_on_route_specifier_config_destroy(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr) {}

envoy_dynamic_module_type_route_specifier_decision envoy_dynamic_module_on_route_specifier_on_route(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr) {
  return envoy_dynamic_module_type_route_specifier_decision_PassThrough;
}
