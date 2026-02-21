#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// Test load balancer that exercises all callback functions for coverage.

typedef struct {
  size_t next_index;
  // Track callback invocation for testing.
  int callbacks_tested;
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

  lb_state* state = (lb_state*)malloc(sizeof(lb_state));
  if (state == NULL) {
    return NULL;
  }
  state->next_index = 0;
  state->callbacks_tested = 0;

  // Test callbacks during initialization.
  envoy_dynamic_module_type_envoy_buffer cluster_name_result = {NULL, 0};
  envoy_dynamic_module_callback_lb_get_cluster_name(lb_envoy_ptr, &cluster_name_result);

  // Test priority set size.
  size_t priority_size =
      envoy_dynamic_module_callback_lb_get_priority_set_size(lb_envoy_ptr);
  (void)priority_size;

  return state;
}

bool envoy_dynamic_module_on_lb_choose_host(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_lb_module_ptr lb_module_ptr,
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr, uint32_t* result_priority,
    uint32_t* result_index) {
  lb_state* state = (lb_state*)lb_module_ptr;

  // Test all host-related callbacks.
  size_t host_count =
      envoy_dynamic_module_callback_lb_get_hosts_count(lb_envoy_ptr, 0);
  size_t healthy_count =
      envoy_dynamic_module_callback_lb_get_healthy_hosts_count(lb_envoy_ptr, 0);
  size_t degraded_count =
      envoy_dynamic_module_callback_lb_get_degraded_hosts_count(lb_envoy_ptr, 0);
  (void)host_count;
  (void)degraded_count;

  // Test healthy host address callback.
  if (healthy_count > 0) {
    envoy_dynamic_module_type_envoy_buffer address_result = {NULL, 0};
    bool found = envoy_dynamic_module_callback_lb_get_healthy_host_address(
        lb_envoy_ptr, 0, 0, &address_result);
    (void)found;

    // Test healthy host weight callback.
    uint32_t weight = envoy_dynamic_module_callback_lb_get_healthy_host_weight(
        lb_envoy_ptr, 0, 0);
    (void)weight;

    // Test host health callback.
    envoy_dynamic_module_type_host_health health =
        envoy_dynamic_module_callback_lb_get_host_health(lb_envoy_ptr, 0, 0);
    (void)health;
  }

  // Test all-hosts callbacks (address, weight, active requests, connections, locality).
  if (host_count > 0) {
    envoy_dynamic_module_type_envoy_buffer host_address_result = {NULL, 0};
    bool host_found = envoy_dynamic_module_callback_lb_get_host_address(
        lb_envoy_ptr, 0, 0, &host_address_result);
    (void)host_found;

    uint32_t host_weight = envoy_dynamic_module_callback_lb_get_host_weight(
        lb_envoy_ptr, 0, 0);
    (void)host_weight;

    uint64_t active_rq = envoy_dynamic_module_callback_lb_get_host_active_requests(
        lb_envoy_ptr, 0, 0);
    (void)active_rq;

    uint64_t active_cx = envoy_dynamic_module_callback_lb_get_host_active_connections(
        lb_envoy_ptr, 0, 0);
    (void)active_cx;

    envoy_dynamic_module_type_envoy_buffer region = {NULL, 0};
    envoy_dynamic_module_type_envoy_buffer zone = {NULL, 0};
    envoy_dynamic_module_type_envoy_buffer sub_zone = {NULL, 0};
    bool locality_found = envoy_dynamic_module_callback_lb_get_host_locality(
        lb_envoy_ptr, 0, 0, &region, &zone, &sub_zone);
    (void)locality_found;
  }

  // Test context callbacks if context is available.
  if (context_envoy_ptr != NULL) {
    // Test hash key computation.
    uint64_t hash = 0;
    bool has_hash =
        envoy_dynamic_module_callback_lb_context_compute_hash_key(context_envoy_ptr, &hash);
    (void)has_hash;

    // Test downstream headers size and get all headers.
    size_t headers_size =
        envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(context_envoy_ptr);

    // Test getting all headers at once.
    if (headers_size > 0) {
      envoy_dynamic_module_type_envoy_http_header all_headers[16];
      bool success = envoy_dynamic_module_callback_lb_context_get_downstream_headers(
          context_envoy_ptr, all_headers);
      (void)success;
    }

    // Test getting a header by key.
    envoy_dynamic_module_type_module_buffer method_key = {":method", 7};
    envoy_dynamic_module_type_envoy_buffer method_result = {NULL, 0};
    size_t method_count = 0;
    bool header_found = envoy_dynamic_module_callback_lb_context_get_downstream_header(
        context_envoy_ptr, method_key, &method_result, 0, &method_count);
    (void)header_found;
  }

  if (healthy_count == 0) {
    return false;
  }

  size_t index = state->next_index % healthy_count;
  state->next_index++;
  *result_priority = 0;
  *result_index = (uint32_t)index;
  return true;
}

void envoy_dynamic_module_on_lb_destroy(envoy_dynamic_module_type_lb_module_ptr lb_module_ptr) {
  free((void*)lb_module_ptr);
}

