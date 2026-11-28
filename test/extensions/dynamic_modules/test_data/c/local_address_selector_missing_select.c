#include <stddef.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// A local address selector module without the select hook.
envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_local_address_selector_config_module_ptr
envoy_dynamic_module_on_local_address_selector_config_new(
    envoy_dynamic_module_type_local_address_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config,
    envoy_dynamic_module_type_envoy_buffer cluster_name) {
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_local_address_selector_config_destroy(
    envoy_dynamic_module_type_local_address_selector_config_module_ptr config_module_ptr) {}
