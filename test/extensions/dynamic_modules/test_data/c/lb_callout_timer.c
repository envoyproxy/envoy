#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// Records HTTP callout and timer events for abi_impl_test.

static int config_marker = 0;
static int callout_done_count = 0;
static uint64_t last_callout_id = 0;
static int last_callout_result = -1;
static size_t last_headers_size = 0;
static size_t last_body_size = 0;
static int timer_count = 0;
static int disable_on_timer = 0;
static char last_header_key[128] = {0};
static char last_header_value[128] = {0};

void lb_callout_timer_test_reset(void) {
  callout_done_count = 0;
  last_callout_id = 0;
  last_callout_result = -1;
  last_headers_size = 0;
  last_body_size = 0;
  timer_count = 0;
  disable_on_timer = 0;
  last_header_key[0] = '\0';
  last_header_value[0] = '\0';
}

int lb_callout_timer_test_callout_done_count(void) { return callout_done_count; }

uint64_t lb_callout_timer_test_last_callout_id(void) { return last_callout_id; }

int lb_callout_timer_test_last_result(void) { return last_callout_result; }

size_t lb_callout_timer_test_last_headers_size(void) { return last_headers_size; }

size_t lb_callout_timer_test_last_body_size(void) { return last_body_size; }

int lb_callout_timer_test_timer_count(void) { return timer_count; }

void lb_callout_timer_test_set_disable_on_timer(int disable) { disable_on_timer = disable; }

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_lb_config_module_ptr envoy_dynamic_module_on_lb_config_new(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)name;
  (void)config;
  // Schedule without implementing the hook. Must not register an init target.
  envoy_dynamic_module_callback_lb_config_schedule_on_server_initialized(lb_config_envoy_ptr);
  return &config_marker;
}

void envoy_dynamic_module_on_lb_config_destroy(
    envoy_dynamic_module_type_lb_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

void envoy_dynamic_module_on_lb_config_http_callout_done(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_lb_config_module_ptr lb_config_module_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size) {
  (void)lb_config_envoy_ptr;
  (void)lb_config_module_ptr;
  callout_done_count++;
  last_callout_id = callout_id;
  last_callout_result = (int)result;
  last_headers_size = headers_size;
  last_body_size = 0;
  for (size_t i = 0; i < body_chunks_size; i++) {
    last_body_size += body_chunks[i].length;
  }
  if (headers_size > 0 && headers != NULL) {
    size_t klen = headers[0].key_length < 127 ? headers[0].key_length : 127;
    for (size_t i = 0; i < klen; i++) {
      last_header_key[i] = ((const char*)headers[0].key_ptr)[i];
    }
    last_header_key[klen] = '\0';

    size_t vlen = headers[0].value_length < 127 ? headers[0].value_length : 127;
    for (size_t i = 0; i < vlen; i++) {
      last_header_value[i] = ((const char*)headers[0].value_ptr)[i];
    }
    last_header_value[vlen] = '\0';
  }
}

const char* lb_callout_timer_test_last_header_key(void) { return last_header_key; }

const char* lb_callout_timer_test_last_header_value(void) { return last_header_value; }

void envoy_dynamic_module_on_lb_config_timer(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_lb_config_module_ptr lb_config_module_ptr) {
  (void)lb_config_module_ptr;
  timer_count++;
  if (disable_on_timer) {
    envoy_dynamic_module_callback_lb_config_disable_timer(lb_config_envoy_ptr);
  }
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
  (void)lb_module_ptr;
  (void)context_envoy_ptr;
  size_t healthy_count = envoy_dynamic_module_callback_lb_get_healthy_hosts_count(lb_envoy_ptr, 0);
  if (healthy_count == 0) {
    return false;
  }
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
