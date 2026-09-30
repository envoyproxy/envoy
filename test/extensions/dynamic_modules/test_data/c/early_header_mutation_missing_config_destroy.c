#include <stdbool.h>
#include <stddef.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This module omits config_destroy to test symbol resolution failure.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_early_header_mutation_config_module_ptr
envoy_dynamic_module_on_early_header_mutation_config_new(
    envoy_dynamic_module_type_early_header_mutation_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  // Return a dummy pointer.
  static int config_dummy = 0;
  return &config_dummy;
}

bool envoy_dynamic_module_on_early_header_mutation_mutate(
    envoy_dynamic_module_type_early_header_mutation_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr) {
  // Do nothing and continue the chain.
  return true;
}
