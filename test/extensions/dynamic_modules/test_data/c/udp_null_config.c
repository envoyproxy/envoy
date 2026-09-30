#include "source/extensions/dynamic_modules/abi/abi.h"

// A UDP listener filter module whose config_new returns null to report a configuration error. Used
// to verify the host rejects the filter at config load instead of passing a null config to a later
// hook.

static int some_variable = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_udp_listener_filter_config_module_ptr
envoy_dynamic_module_on_udp_listener_filter_config_new(
    envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)filter_config_envoy_ptr;
  (void)name;
  (void)config;
  return NULL;
}

void envoy_dynamic_module_on_udp_listener_filter_config_destroy(
    envoy_dynamic_module_type_udp_listener_filter_config_module_ptr filter_config_ptr) {
  (void)filter_config_ptr;
}

envoy_dynamic_module_type_udp_listener_filter_module_ptr
envoy_dynamic_module_on_udp_listener_filter_new(
    envoy_dynamic_module_type_udp_listener_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr) {
  (void)filter_config_ptr;
  (void)filter_envoy_ptr;
  return &some_variable;
}

envoy_dynamic_module_type_on_udp_listener_filter_status
envoy_dynamic_module_on_udp_listener_filter_on_data(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_udp_listener_filter_module_ptr filter_module_ptr) {
  (void)filter_envoy_ptr;
  (void)filter_module_ptr;
  return envoy_dynamic_module_type_on_udp_listener_filter_status_Continue;
}

void envoy_dynamic_module_on_udp_listener_filter_destroy(
    envoy_dynamic_module_type_udp_listener_filter_module_ptr filter_module_ptr) {
  (void)filter_module_ptr;
}
