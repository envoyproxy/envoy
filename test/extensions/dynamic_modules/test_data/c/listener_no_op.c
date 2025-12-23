#include <assert.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

static int some_variable = 0;

int getListenerSomeVariable(void) {
  some_variable++;
  return some_variable;
}

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return kAbiVersion;
}

envoy_dynamic_module_type_listener_filter_config_module_ptr
envoy_dynamic_module_on_listener_filter_config_new(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)filter_config_envoy_ptr;
  (void)name;
  (void)config;
  return &some_variable;
}

void envoy_dynamic_module_on_listener_filter_config_destroy(
    envoy_dynamic_module_type_listener_filter_config_module_ptr filter_config_ptr) {
  assert(filter_config_ptr == &some_variable);
}

envoy_dynamic_module_type_listener_filter_module_ptr envoy_dynamic_module_on_listener_filter_new(
    envoy_dynamic_module_type_listener_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  return &some_variable + 1;
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
  // Return 0 to indicate no data inspection is needed.
  return 0;
}

void envoy_dynamic_module_on_listener_filter_destroy(
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr) {
  assert(filter_module_ptr == &some_variable + 1);
}
