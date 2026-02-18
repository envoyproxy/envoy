#include "source/extensions/dynamic_modules/abi/abi.h"


// This module returns nullptr from config_new to test error handling.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_access_logger_config_module_ptr
envoy_dynamic_module_on_access_logger_config_new(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  // Return nullptr to simulate initialization failure.
  return NULL;
}

void envoy_dynamic_module_on_access_logger_config_destroy(
    envoy_dynamic_module_type_access_logger_config_module_ptr config_module_ptr) {}

envoy_dynamic_module_type_access_logger_module_ptr
envoy_dynamic_module_on_access_logger_new(
    envoy_dynamic_module_type_access_logger_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  return NULL;
}

void envoy_dynamic_module_on_access_logger_log(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_access_logger_module_ptr logger_module_ptr,
    envoy_dynamic_module_type_access_log_type log_type) {}

void envoy_dynamic_module_on_access_logger_destroy(
    envoy_dynamic_module_type_access_logger_module_ptr logger_module_ptr) {}
