#include <stddef.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This module omits envoy_dynamic_module_on_ai_filter_request to test symbol resolution failure.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_ai_filter_config_module_ptr envoy_dynamic_module_on_ai_filter_config_new(
    envoy_dynamic_module_type_ai_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_ai_filter_config_destroy(
    envoy_dynamic_module_type_ai_filter_config_module_ptr config_module_ptr) {}

envoy_dynamic_module_type_on_ai_filter_new_status
envoy_dynamic_module_on_ai_filter_new(envoy_dynamic_module_type_ai_filter_config_module_ptr config,
                                      envoy_dynamic_module_type_ai_filter_envoy_ptr filter,
                                      envoy_dynamic_module_type_ai_filter_module_ptr* filter_out) {
  *filter_out = config;
  return envoy_dynamic_module_type_on_ai_filter_new_status_Created;
}

void envoy_dynamic_module_on_ai_filter_destroy(
    envoy_dynamic_module_type_ai_filter_module_ptr filter_module_ptr) {}
