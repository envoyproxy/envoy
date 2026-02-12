#include "source/extensions/dynamic_modules/abi/abi.h"


envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

// Returns nullptr to simulate configuration initialization failure.
envoy_dynamic_module_type_listener_filter_config_module_ptr
envoy_dynamic_module_on_listener_filter_config_new(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)filter_config_envoy_ptr;
  (void)name;
  (void)config;
  return 0; // Return nullptr to indicate failure.
}

void envoy_dynamic_module_on_listener_filter_config_destroy(
    envoy_dynamic_module_type_listener_filter_config_module_ptr filter_config_ptr) {}

envoy_dynamic_module_type_listener_filter_module_ptr envoy_dynamic_module_on_listener_filter_new(
    envoy_dynamic_module_type_listener_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  return 0;
}

envoy_dynamic_module_type_on_listener_filter_status
envoy_dynamic_module_on_listener_filter_on_accept(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr) {
  return envoy_dynamic_module_type_on_listener_filter_status_Continue;
}

envoy_dynamic_module_type_on_listener_filter_status
envoy_dynamic_module_on_listener_filter_on_data(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr, size_t data_length) {
  return envoy_dynamic_module_type_on_listener_filter_status_Continue;
}

void envoy_dynamic_module_on_listener_filter_on_close(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr) {}

size_t envoy_dynamic_module_on_listener_filter_get_max_read_bytes(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr) {
  return 0;
}

void envoy_dynamic_module_on_listener_filter_destroy(
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr) {}

void envoy_dynamic_module_on_listener_filter_scheduled(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr, uint64_t event_id) {
  (void)filter_envoy_ptr;
  (void)filter_module_ptr;
  (void)event_id;
}

void envoy_dynamic_module_on_listener_filter_config_scheduled(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_config_module_ptr filter_config_module_ptr,
    uint64_t event_id) {
  (void)filter_config_envoy_ptr;
  (void)filter_config_module_ptr;
  (void)event_id;
}
