#include <stdbool.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This is a minimal implementation of a cluster specifier module.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_cluster_specifier_config_module_ptr
envoy_dynamic_module_on_cluster_specifier_config_new(
    envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  // Return a dummy pointer.
  static int config_dummy = 0;
  return &config_dummy;
}

static int config_destroy_count = 0;

// Lets tests observe that Envoy invoked the config destroy hook.
int getConfigDestroyCount(void) { return config_destroy_count; }

void envoy_dynamic_module_on_cluster_specifier_config_destroy(
    envoy_dynamic_module_type_cluster_specifier_config_module_ptr config_module_ptr) {
  config_destroy_count++;
}

bool envoy_dynamic_module_on_cluster_specifier_select(
    envoy_dynamic_module_type_cluster_specifier_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr) {
  // Do nothing.
  return false;
}
