#include "source/extensions/dynamic_modules/abi/abi.h"

static int config_dummy = 0;
static int config_destroy_count = 0;

// Lets tests observe how often Envoy destroyed a configuration.
int getConfigDestroyCount(void) { return config_destroy_count; }

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_resource_monitor_config_module_ptr
envoy_dynamic_module_on_resource_monitor_config_new(
    envoy_dynamic_module_type_resource_monitor_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)name;
  (void)config;
  envoy_dynamic_module_callback_resource_monitor_config_enable_synchronous_feedback(
      config_envoy_ptr);
  return &config_dummy;
}

void envoy_dynamic_module_on_resource_monitor_config_destroy(
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
  ++config_destroy_count;
}

void envoy_dynamic_module_on_resource_monitor_load_accepted(
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer load_shed_point_name) {
  (void)config_module_ptr;
  (void)load_shed_point_name;
}
