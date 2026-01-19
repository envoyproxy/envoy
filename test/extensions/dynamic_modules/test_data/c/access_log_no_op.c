#include <assert.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

// This is a minimal implementation of an access logger module.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return kAbiVersion;
}

envoy_dynamic_module_type_access_logger_config_module_ptr
envoy_dynamic_module_on_access_logger_config_new(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  // Return a dummy pointer.
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_access_logger_config_destroy(
    envoy_dynamic_module_type_access_logger_config_module_ptr config_module_ptr) {}

envoy_dynamic_module_type_access_logger_module_ptr
envoy_dynamic_module_on_access_logger_new(
    envoy_dynamic_module_type_access_logger_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  // Return a dummy pointer.
  static int logger_dummy = 0;
  return &logger_dummy;
}

void envoy_dynamic_module_on_access_logger_log(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_access_logger_module_ptr logger_module_ptr,
    envoy_dynamic_module_type_access_log_type log_type) {
  // Do nothing.
}

void envoy_dynamic_module_on_access_logger_destroy(
    envoy_dynamic_module_type_access_logger_module_ptr logger_module_ptr) {}

void envoy_dynamic_module_on_access_logger_flush(
    envoy_dynamic_module_type_access_logger_module_ptr logger_module_ptr) {}
