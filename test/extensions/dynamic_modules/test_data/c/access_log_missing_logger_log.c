#include <assert.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"


envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_access_logger_config_module_ptr
envoy_dynamic_module_on_access_logger_config_new(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_access_logger_config_destroy(
    envoy_dynamic_module_type_access_logger_config_module_ptr config_module_ptr) {}

envoy_dynamic_module_type_access_logger_module_ptr
envoy_dynamic_module_on_access_logger_new(
    envoy_dynamic_module_type_access_logger_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  static int logger_dummy = 0;
  return &logger_dummy;
}
