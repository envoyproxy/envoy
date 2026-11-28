#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// The null_worker filter fails during worker construction. Other names fail during config load.
static int valid_config;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_udp_listener_filter_config_module_ptr
envoy_dynamic_module_on_udp_listener_filter_config_new(
    envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)filter_config_envoy_ptr;
  (void)config;
  static const char null_worker[] = "null_worker";
  if (name.length == sizeof(null_worker) - 1 && memcmp(name.ptr, null_worker, name.length) == 0) {
    return &valid_config;
  }
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
  return NULL;
}

envoy_dynamic_module_type_on_udp_listener_filter_status
envoy_dynamic_module_on_udp_listener_filter_on_data(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_udp_listener_filter_module_ptr filter_module_ptr) {
  (void)filter_envoy_ptr;
  (void)filter_module_ptr;
  return envoy_dynamic_module_type_on_udp_listener_filter_status_StopIteration;
}

void envoy_dynamic_module_on_udp_listener_filter_destroy(
    envoy_dynamic_module_type_udp_listener_filter_module_ptr filter_module_ptr) {
  (void)filter_module_ptr;
}
