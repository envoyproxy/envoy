#include <assert.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

static int some_variable = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_udp_session_filter_config_module_ptr
envoy_dynamic_module_on_udp_session_filter_config_new(
    envoy_dynamic_module_type_udp_session_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)filter_config_envoy_ptr;
  (void)name;
  (void)config;
  return &some_variable;
}

void envoy_dynamic_module_on_udp_session_filter_config_destroy(
    envoy_dynamic_module_type_udp_session_filter_config_module_ptr filter_config_ptr) {
  assert(filter_config_ptr == &some_variable);
}

envoy_dynamic_module_type_udp_session_filter_module_ptr
envoy_dynamic_module_on_udp_session_filter_new(
    envoy_dynamic_module_type_udp_session_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr) {
  (void)filter_config_ptr;
  (void)filter_envoy_ptr;
  return &some_variable + 1;
}

envoy_dynamic_module_type_on_udp_session_read_filter_status
envoy_dynamic_module_on_udp_session_filter_on_new_session(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_udp_session_filter_module_ptr filter_module_ptr) {
  (void)filter_envoy_ptr;
  (void)filter_module_ptr;
  return envoy_dynamic_module_type_on_udp_session_read_filter_status_Continue;
}

envoy_dynamic_module_type_on_udp_session_read_filter_status
envoy_dynamic_module_on_udp_session_filter_on_data(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_udp_session_filter_module_ptr filter_module_ptr) {
  (void)filter_envoy_ptr;
  (void)filter_module_ptr;
  return envoy_dynamic_module_type_on_udp_session_read_filter_status_Continue;
}

envoy_dynamic_module_type_on_udp_session_write_filter_status
envoy_dynamic_module_on_udp_session_filter_on_write(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_udp_session_filter_module_ptr filter_module_ptr) {
  (void)filter_envoy_ptr;
  (void)filter_module_ptr;
  return envoy_dynamic_module_type_on_udp_session_write_filter_status_Continue;
}

void envoy_dynamic_module_on_udp_session_filter_on_session_complete(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_udp_session_filter_module_ptr filter_module_ptr) {
  (void)filter_envoy_ptr;
  (void)filter_module_ptr;
}

void envoy_dynamic_module_on_udp_session_filter_destroy(
    envoy_dynamic_module_type_udp_session_filter_module_ptr filter_module_ptr) {
  assert(filter_module_ptr == &some_variable + 1);
}
