#include <assert.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

static int some_variable = 0;

int getNetworkSomeVariable(void) {
  some_variable++;
  return some_variable;
}

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return kAbiVersion;
}

envoy_dynamic_module_type_network_filter_config_module_ptr
envoy_dynamic_module_on_network_filter_config_new(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  return &some_variable;
}

void envoy_dynamic_module_on_network_filter_config_destroy(
    envoy_dynamic_module_type_network_filter_config_module_ptr filter_config_ptr) {
}

envoy_dynamic_module_type_network_filter_module_ptr envoy_dynamic_module_on_network_filter_new(
    envoy_dynamic_module_type_network_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  // Intentionally return nullptr to simulate filter initialization failure.
  return 0;
}

envoy_dynamic_module_type_on_network_filter_data_status
envoy_dynamic_module_on_network_filter_new_connection(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr) {
  return envoy_dynamic_module_type_on_network_filter_data_status_Continue;
}

envoy_dynamic_module_type_on_network_filter_data_status envoy_dynamic_module_on_network_filter_read(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr, size_t data_length,
    bool end_stream) {
  return envoy_dynamic_module_type_on_network_filter_data_status_Continue;
}

envoy_dynamic_module_type_on_network_filter_data_status
envoy_dynamic_module_on_network_filter_write(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr, size_t data_length,
    bool end_stream) {
  return envoy_dynamic_module_type_on_network_filter_data_status_Continue;
}

void envoy_dynamic_module_on_network_filter_event(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr,
    envoy_dynamic_module_type_network_connection_event event) {}

void envoy_dynamic_module_on_network_filter_destroy(
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr) {}
