// A header formatter module that loads and configures fine but declines to create a
// per-message formatter, which must leave Envoy using its default header casing.
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_header_formatter_config_module_ptr
envoy_dynamic_module_on_header_formatter_config_new(
    envoy_dynamic_module_type_header_formatter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_header_formatter_config_destroy(
    envoy_dynamic_module_type_header_formatter_config_module_ptr config_module_ptr) {}

envoy_dynamic_module_type_header_formatter_module_ptr envoy_dynamic_module_on_header_formatter_new(
    envoy_dynamic_module_type_header_formatter_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_header_formatter_envoy_ptr formatter_envoy_ptr) {
  return NULL;
}

void envoy_dynamic_module_on_header_formatter_destroy(
    envoy_dynamic_module_type_header_formatter_module_ptr formatter_module_ptr) {}

void envoy_dynamic_module_on_header_formatter_process_key(
    envoy_dynamic_module_type_header_formatter_envoy_ptr formatter_envoy_ptr,
    envoy_dynamic_module_type_header_formatter_module_ptr formatter_module_ptr,
    envoy_dynamic_module_type_envoy_buffer key) {}

bool envoy_dynamic_module_on_header_formatter_format(
    envoy_dynamic_module_type_header_formatter_envoy_ptr formatter_envoy_ptr,
    envoy_dynamic_module_type_header_formatter_module_ptr formatter_module_ptr,
    envoy_dynamic_module_type_envoy_buffer key, envoy_dynamic_module_type_module_buffer* result) {
  return false;
}
