#include <stdbool.h>
#include <stddef.h>

#include "source/extensions/dynamic_modules/abi/abi.h"


// This module is missing envoy_dynamic_module_on_formatter_format.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_formatter_config_module_ptr
envoy_dynamic_module_on_formatter_config_new(
    envoy_dynamic_module_type_formatter_config_envoy_ptr formatter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_formatter_config_destroy(
    envoy_dynamic_module_type_formatter_config_module_ptr config_module_ptr) {}

envoy_dynamic_module_type_formatter_provider_module_ptr
envoy_dynamic_module_on_formatter_parse(
    envoy_dynamic_module_type_formatter_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer command,
    envoy_dynamic_module_type_envoy_buffer command_arg, bool has_max_length, size_t max_length) {
  static int provider_dummy = 0;
  return &provider_dummy;
}

void envoy_dynamic_module_on_formatter_provider_destroy(
    envoy_dynamic_module_type_formatter_provider_module_ptr provider_module_ptr) {}
