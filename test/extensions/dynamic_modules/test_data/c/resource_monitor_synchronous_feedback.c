#include "source/extensions/dynamic_modules/abi/abi.h"

static int config_dummy = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_resource_monitor_config_module_ptr
envoy_dynamic_module_on_resource_monitor_config_new(
    envoy_dynamic_module_type_resource_monitor_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)name;
  (void)config;
  if (!envoy_dynamic_module_callback_resource_monitor_config_enable_synchronous_feedback(
          config_envoy_ptr)) {
    return NULL;
  }
  return &config_dummy;
}

void envoy_dynamic_module_on_resource_monitor_config_destroy(
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

// Envoy gets the pressure from envoy_dynamic_module_on_resource_monitor_get_resource_usage instead,
// since the module enables synchronous feedback.
envoy_dynamic_module_type_resource_monitor_update_status
envoy_dynamic_module_on_resource_monitor_update_resource_usage(
    envoy_dynamic_module_type_resource_monitor_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr,
    double* resource_pressure) {
  (void)config_envoy_ptr;
  (void)config_module_ptr;
  *resource_pressure = 1;
  return envoy_dynamic_module_type_resource_monitor_update_status_Success;
}

double envoy_dynamic_module_on_resource_monitor_get_resource_usage(
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
  return 0.25;
}

void envoy_dynamic_module_on_resource_monitor_load_accepted(
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer load_shed_point_name) {
  (void)config_module_ptr;
  (void)load_shed_point_name;
}
