#include "source/extensions/dynamic_modules/abi/abi.h"

// A matcher module that simulates a match hook which could not complete. It reports the error
// through the ABI exactly as the SDK panic barrier does and returns false. The host must then apply
// its on_error policy rather than treating the result as a no match.
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

bool envoy_dynamic_module_on_matcher_match(
    envoy_dynamic_module_type_matcher_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_matcher_input_envoy_ptr matcher_input_envoy_ptr) {
  envoy_dynamic_module_callback_matcher_set_error(matcher_input_envoy_ptr);
  return false;
}
