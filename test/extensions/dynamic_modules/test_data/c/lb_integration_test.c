#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// Integration test dynamic module exercising server_initialized, timer, and HTTP callout.

static int config_marker = 0;
static int lb_marker = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_lb_config_module_ptr envoy_dynamic_module_on_lb_config_new(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)name;
  (void)config;
  envoy_dynamic_module_callback_lb_config_schedule_on_server_initialized(lb_config_envoy_ptr);
  envoy_dynamic_module_callback_lb_config_enable_timer(lb_config_envoy_ptr, 10);
  return &config_marker;
}

void envoy_dynamic_module_on_lb_config_destroy(
    envoy_dynamic_module_type_lb_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

void envoy_dynamic_module_on_lb_config_server_initialized(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_lb_config_module_ptr lb_config_module_ptr) {
  (void)lb_config_module_ptr;
  envoy_dynamic_module_type_module_buffer msg = {"lb_integration_test: server_initialized", 39};
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Info, msg);

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_http_header headers[3] = {
      {":method", 7, "GET", 3},
      {":path", 5, "/callout", 8},
      {"host", 4, "example.com", 11},
  };
  envoy_dynamic_module_type_module_buffer cluster_name = {"cluster_0", 9};
  envoy_dynamic_module_type_module_buffer body = {NULL, 0};
  envoy_dynamic_module_callback_lb_config_http_callout(
      lb_config_envoy_ptr, &callout_id, cluster_name, headers, 3, body, 5000);
}

void envoy_dynamic_module_on_lb_config_http_callout_done(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_lb_config_module_ptr lb_config_module_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size) {
  (void)lb_config_envoy_ptr;
  (void)lb_config_module_ptr;
  (void)callout_id;
  (void)result;
  (void)headers;
  (void)headers_size;
  (void)body_chunks;
  (void)body_chunks_size;
  envoy_dynamic_module_type_module_buffer msg = {"lb_integration_test: callout_done", 33};
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Info, msg);
}

void envoy_dynamic_module_on_lb_config_timer(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_lb_config_module_ptr lb_config_module_ptr) {
  (void)lb_config_module_ptr;
  envoy_dynamic_module_callback_lb_config_disable_timer(lb_config_envoy_ptr);
  envoy_dynamic_module_type_module_buffer msg = {"lb_integration_test: timer_fired", 32};
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Info, msg);
}

envoy_dynamic_module_type_lb_module_ptr
envoy_dynamic_module_on_lb_new(envoy_dynamic_module_type_lb_config_module_ptr config_module_ptr,
                               envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr) {
  (void)config_module_ptr;
  (void)lb_envoy_ptr;
  return &lb_marker;
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
