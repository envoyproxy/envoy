#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// Same hooks as lb_server_initialized_test, without the schedule call.

static int config_marker = 0;
static int called = 0;

int lb_server_initialized_no_schedule_called(void) { return called; }

void lb_server_initialized_no_schedule_reset(void) { called = 0; }

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_lb_config_module_ptr envoy_dynamic_module_on_lb_config_new(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)lb_config_envoy_ptr;
  (void)name;
  (void)config;
  return &config_marker;
}

void envoy_dynamic_module_on_lb_config_destroy(
    envoy_dynamic_module_type_lb_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

void envoy_dynamic_module_on_lb_config_server_initialized(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_lb_config_module_ptr lb_config_module_ptr) {
  (void)lb_config_envoy_ptr;
  (void)lb_config_module_ptr;
  called++;
}

envoy_dynamic_module_type_lb_module_ptr
envoy_dynamic_module_on_lb_new(envoy_dynamic_module_type_lb_config_module_ptr config_module_ptr,
                               envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr) {
  (void)config_module_ptr;
  (void)lb_envoy_ptr;
  return &config_marker;
}

bool envoy_dynamic_module_on_lb_choose_host(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_lb_module_ptr lb_module_ptr,
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr, uint32_t* result_priority,
    uint32_t* result_index) {
  (void)lb_envoy_ptr;
  (void)lb_module_ptr;
  (void)context_envoy_ptr;
  *result_priority = 0;
  *result_index = 0;
  return true;
}

void envoy_dynamic_module_on_lb_on_host_membership_update(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_lb_module_ptr lb_module_ptr, size_t num_hosts_added,
    size_t num_hosts_removed) {
  (void)lb_envoy_ptr;
  (void)lb_module_ptr;
  (void)num_hosts_added;
  (void)num_hosts_removed;
}

void envoy_dynamic_module_on_lb_destroy(envoy_dynamic_module_type_lb_module_ptr lb_module_ptr) {
  (void)lb_module_ptr;
}
