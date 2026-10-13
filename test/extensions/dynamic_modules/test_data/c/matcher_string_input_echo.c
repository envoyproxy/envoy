#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// Data input module that echoes the request header named by its configuration. The module stores
// the header name from the config bytes, reads that header during get, and hands the value back
// through set_result. It also counts configuration destroys so a test can assert the destroy runs
// exactly once.
static int string_input_config_destroy_count = 0;

int getStringDataInputConfigDestroyCount(void) { return string_input_config_destroy_count; }

typedef struct {
  char* header_name;
  size_t header_name_length;
} string_input_config_t;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_matcher_data_input_config_module_ptr
envoy_dynamic_module_on_matcher_data_input_config_new(
    envoy_dynamic_module_type_matcher_data_input_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  string_input_config_t* stored = (string_input_config_t*)malloc(sizeof(string_input_config_t));
  stored->header_name = (char*)malloc(config.length);
  memcpy(stored->header_name, config.ptr, config.length);
  stored->header_name_length = config.length;
  return stored;
}

void envoy_dynamic_module_on_matcher_data_input_config_destroy(
    envoy_dynamic_module_type_matcher_data_input_config_module_ptr config_module_ptr) {
  string_input_config_t* stored = (string_input_config_t*)config_module_ptr;
  if (stored != NULL) {
    free(stored->header_name);
    free(stored);
  }
  string_input_config_destroy_count++;
}

void envoy_dynamic_module_on_matcher_data_input_get(
    envoy_dynamic_module_type_matcher_data_input_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_matcher_data_input_envoy_ptr data_input_envoy_ptr) {
  string_input_config_t* stored = (string_input_config_t*)config_module_ptr;
  if (stored == NULL) {
    return;
  }
  envoy_dynamic_module_type_module_buffer key = {stored->header_name, stored->header_name_length};
  envoy_dynamic_module_type_envoy_buffer value;
  bool found = envoy_dynamic_module_callback_matcher_data_input_get_header_value(
      data_input_envoy_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, key, &value,
      0, NULL);
  if (!found) {
    return;
  }
  envoy_dynamic_module_type_module_buffer result = {value.ptr, value.length};
  envoy_dynamic_module_callback_matcher_data_input_set_result(data_input_envoy_ptr, result);
}
