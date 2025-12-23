#include <stdlib.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return kAbiVersion;
}

envoy_dynamic_module_type_network_filter_config_module_ptr
envoy_dynamic_module_on_network_filter_config_new(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr filter_config_envoy_ptr,
    const char* name_ptr, size_t name_size, const char* config_ptr, size_t config_size) {
  // Return a non-null value to indicate success.
  int* config = (int*)malloc(sizeof(int));
  *config = 0;
  return config;
}

void envoy_dynamic_module_on_network_filter_config_destroy(
    envoy_dynamic_module_type_network_filter_config_module_ptr filter_config_ptr) {
  free((void*)filter_config_ptr);
}

envoy_dynamic_module_type_network_filter_module_ptr envoy_dynamic_module_on_network_filter_new(
    envoy_dynamic_module_type_network_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  int* filter = (int*)malloc(sizeof(int));
  *filter = 0;
  return filter;
}

envoy_dynamic_module_type_on_network_filter_data_status
envoy_dynamic_module_on_network_filter_new_connection(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr) {
  // Return StopIteration.
  return envoy_dynamic_module_type_on_network_filter_data_status_StopIteration;
}

envoy_dynamic_module_type_on_network_filter_data_status envoy_dynamic_module_on_network_filter_read(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr, size_t data_length,
    bool end_stream) {
  // Return StopIteration.
  return envoy_dynamic_module_type_on_network_filter_data_status_StopIteration;
}

envoy_dynamic_module_type_on_network_filter_data_status
envoy_dynamic_module_on_network_filter_write(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr, size_t data_length,
    bool end_stream) {
  // Return StopIteration.
  return envoy_dynamic_module_type_on_network_filter_data_status_StopIteration;
}

void envoy_dynamic_module_on_network_filter_event(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr,
    envoy_dynamic_module_type_network_connection_event event) {}

void envoy_dynamic_module_on_network_filter_destroy(
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr) {
  free((void*)filter_module_ptr);
}
