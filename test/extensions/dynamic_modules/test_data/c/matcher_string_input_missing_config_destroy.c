#include "source/extensions/dynamic_modules/abi/abi.h"

// Data input module missing the config_destroy hook, so symbol resolution fails at load.
envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_matcher_data_input_config_module_ptr
envoy_dynamic_module_on_matcher_data_input_config_new(
    envoy_dynamic_module_type_matcher_data_input_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_matcher_data_input_get(
    envoy_dynamic_module_type_matcher_data_input_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_matcher_data_input_envoy_ptr data_input_envoy_ptr) {}
