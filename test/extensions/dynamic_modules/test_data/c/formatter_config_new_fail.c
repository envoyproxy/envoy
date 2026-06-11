#include <stdbool.h>
#include <stddef.h>

#include "source/extensions/dynamic_modules/abi/abi.h"


// This module returns nullptr from config_new to test error handling.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_formatter_config_module_ptr
envoy_dynamic_module_on_formatter_config_new(
    envoy_dynamic_module_type_formatter_config_envoy_ptr formatter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  // Return nullptr to simulate initialization failure.
  return NULL;
}

void envoy_dynamic_module_on_formatter_config_destroy(
    envoy_dynamic_module_type_formatter_config_module_ptr config_module_ptr) {}

envoy_dynamic_module_type_formatter_provider_module_ptr
envoy_dynamic_module_on_formatter_parse(
    envoy_dynamic_module_type_formatter_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer command,
    envoy_dynamic_module_type_envoy_buffer command_arg, bool has_max_length, size_t max_length) {
  return NULL;
}

void envoy_dynamic_module_on_formatter_provider_destroy(
    envoy_dynamic_module_type_formatter_provider_module_ptr provider_module_ptr) {}

bool envoy_dynamic_module_on_formatter_format(
    envoy_dynamic_module_type_formatter_provider_module_ptr provider_module_ptr,
    envoy_dynamic_module_type_formatter_context_envoy_ptr formatter_context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer* result) {
  return false;
}
