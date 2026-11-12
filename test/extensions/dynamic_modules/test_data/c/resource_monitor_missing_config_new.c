#include "source/extensions/dynamic_modules/abi/abi.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

void envoy_dynamic_module_on_resource_monitor_config_destroy(
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_resource_monitor_update_status
envoy_dynamic_module_on_resource_monitor_update_resource_usage(
    envoy_dynamic_module_type_resource_monitor_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr,
    double* resource_pressure) {
  (void)config_envoy_ptr;
  (void)config_module_ptr;
  *resource_pressure = 0;
  return envoy_dynamic_module_type_resource_monitor_update_status_Success;
}
