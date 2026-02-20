#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// Simple round-robin load balancer implementation for testing.

typedef struct {
  size_t next_index;
} lb_state;

static int config_marker = 0;

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

envoy_dynamic_module_type_lb_module_ptr
envoy_dynamic_module_on_lb_new(envoy_dynamic_module_type_lb_config_module_ptr config_module_ptr,
                               envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr) {
  (void)config_module_ptr;
  (void)lb_envoy_ptr;
  lb_state* state = (lb_state*)malloc(sizeof(lb_state));
  if (state == NULL) {
    return NULL;
  }
  state->next_index = 0;
  return state;
}

bool envoy_dynamic_module_on_lb_choose_host(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_lb_module_ptr lb_module_ptr,
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr, uint32_t* result_priority,
    uint32_t* result_index) {
  (void)context_envoy_ptr;

  lb_state* state = (lb_state*)lb_module_ptr;

  size_t host_count =
      envoy_dynamic_module_callback_lb_get_healthy_hosts_count(lb_envoy_ptr, 0);
  if (host_count == 0) {
    return false;
  }

  size_t index = state->next_index % host_count;
  state->next_index++;
  *result_priority = 0;
  *result_index = (uint32_t)index;
  return true;
}

void envoy_dynamic_module_on_lb_destroy(envoy_dynamic_module_type_lb_module_ptr lb_module_ptr) {
  free((void*)lb_module_ptr);
}

