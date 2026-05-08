#include "source/extensions/dynamic_modules/abi/abi.h"

// This module provides config_new but is missing the match function.
envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_matcher_config_module_ptr envoy_dynamic_module_on_matcher_config_new(
    envoy_dynamic_module_type_matcher_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer matcher_name,
    envoy_dynamic_module_type_envoy_buffer matcher_config) {
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_matcher_config_destroy(
    envoy_dynamic_module_type_matcher_config_module_ptr config_module_ptr) {}

