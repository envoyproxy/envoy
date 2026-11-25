#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// Integration test dynamic module exercising server_initialized, timer, and HTTP callout.

static int config_marker = 0;
static int lb_marker = 0;

static size_t timer_fired_counter_id = 0;
static size_t callout_success_counter_id = 0;
static size_t startup_callout_cluster_not_found_counter_id = 0;
static size_t server_initialized_counter_id = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_lb_config_module_ptr envoy_dynamic_module_on_lb_config_new(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)name;
  (void)config;

  envoy_dynamic_module_type_module_buffer timer_metric = {"timer_fired", 11};
  envoy_dynamic_module_callback_lb_config_define_counter(lb_config_envoy_ptr, timer_metric, NULL,
                                                         0, &timer_fired_counter_id);

  envoy_dynamic_module_type_module_buffer callout_metric = {"callout_success", 15};
  envoy_dynamic_module_callback_lb_config_define_counter(lb_config_envoy_ptr, callout_metric,
                                                         NULL, 0, &callout_success_counter_id);

  envoy_dynamic_module_type_module_buffer startup_metric = {"startup_callout_cluster_not_found",
                                                            33};
  envoy_dynamic_module_callback_lb_config_define_counter(
      lb_config_envoy_ptr, startup_metric, NULL, 0, &startup_callout_cluster_not_found_counter_id);

  envoy_dynamic_module_type_module_buffer server_init_metric = {"server_initialized", 18};
  envoy_dynamic_module_callback_lb_config_define_counter(
      lb_config_envoy_ptr, server_init_metric, NULL, 0, &server_initialized_counter_id);

  // Attempt callout before server is initialized. Must return ClusterNotFound.
  uint64_t startup_callout_id = 0;
  envoy_dynamic_module_type_module_http_header startup_headers[3] = {
      {":method", 7, "GET", 3},
      {":path", 5, "/callout", 8},
      {"host", 4, "example.com", 11},
  };
  envoy_dynamic_module_type_module_buffer cluster_name = {"cluster_0", 9};
  envoy_dynamic_module_type_module_buffer body = {NULL, 0};
  envoy_dynamic_module_type_http_callout_init_result startup_res =
      envoy_dynamic_module_callback_lb_config_http_callout(
          lb_config_envoy_ptr, &startup_callout_id, cluster_name, startup_headers, 3, body, 5000);
  if (startup_res == envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound) {
    envoy_dynamic_module_callback_lb_config_increment_counter(
        lb_config_envoy_ptr, startup_callout_cluster_not_found_counter_id, NULL, 0, 1);
  }

  if (!envoy_dynamic_module_callback_lb_config_schedule_on_server_initialized(
          lb_config_envoy_ptr)) {
    return NULL;
  }
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
  envoy_dynamic_module_callback_lb_config_increment_counter(
      lb_config_envoy_ptr, server_initialized_counter_id, NULL, 0, 1);
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
  (void)body_chunks;
  (void)body_chunks_size;

  int status_200 = 0;
  for (size_t i = 0; i < headers_size; i++) {
    if (headers[i].key_length == 7 && headers[i].value_length == 3 &&
        memcmp(headers[i].key_ptr, ":status", 7) == 0 &&
        memcmp(headers[i].value_ptr, "200", 3) == 0) {
      status_200 = 1;
      break;
    }
  }
  if (status_200 && result == envoy_dynamic_module_type_http_callout_result_Success &&
      callout_id != 0) {
    envoy_dynamic_module_callback_lb_config_increment_counter(
        lb_config_envoy_ptr, callout_success_counter_id, NULL, 0, 1);
  }

  envoy_dynamic_module_type_module_buffer msg = {"lb_integration_test: callout_done", 33};
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Info, msg);
}

void envoy_dynamic_module_on_lb_config_timer(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_lb_config_module_ptr lb_config_module_ptr) {
  (void)lb_config_module_ptr;
  envoy_dynamic_module_callback_lb_config_increment_counter(
      lb_config_envoy_ptr, timer_fired_counter_id, NULL, 0, 1);
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
