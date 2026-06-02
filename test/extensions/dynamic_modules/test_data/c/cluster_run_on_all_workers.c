// Test cluster module exercising the worker fan-out and worker_slot_set/get destroy hook. The
// destroy hook free()s the malloc'd payload the test publishes; the test reads these atomics
// to verify counts.

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

typedef struct {
  // Number of times on_cluster_worker_event has fired across all workers.
  atomic_size_t worker_event_count;
  // Last event_id observed.
  atomic_uint_least64_t last_event_id;
  // Number of times the destroy hook has fired (one per published payload that has been released).
  atomic_size_t destroy_count;
} cluster_state;

static cluster_state g_state = {0, 0, 0};

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_cluster_config_module_ptr envoy_dynamic_module_on_cluster_config_new(
    envoy_dynamic_module_type_cluster_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  return (envoy_dynamic_module_type_cluster_config_module_ptr)0x1;
}

void envoy_dynamic_module_on_cluster_config_destroy(
    envoy_dynamic_module_type_cluster_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_cluster_module_ptr envoy_dynamic_module_on_cluster_new(
    envoy_dynamic_module_type_cluster_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  (void)config_module_ptr;
  (void)cluster_envoy_ptr;
  // Reset counters at module-instance creation so a single test can observe a clean run.
  atomic_store(&g_state.worker_event_count, 0);
  atomic_store(&g_state.last_event_id, 0);
  atomic_store(&g_state.destroy_count, 0);
  return (envoy_dynamic_module_type_cluster_module_ptr)&g_state;
}

void envoy_dynamic_module_on_cluster_init(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr) {
  (void)cluster_envoy_ptr;
  (void)cluster_module_ptr;
}

void envoy_dynamic_module_on_cluster_destroy(
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr) {
  (void)cluster_module_ptr;
}

envoy_dynamic_module_type_cluster_lb_module_ptr envoy_dynamic_module_on_cluster_lb_new(
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr) {
  (void)cluster_module_ptr;
  (void)lb_envoy_ptr;
  return (envoy_dynamic_module_type_cluster_lb_module_ptr)0x3;
}

void envoy_dynamic_module_on_cluster_lb_destroy(
    envoy_dynamic_module_type_cluster_lb_module_ptr lb_module_ptr) {
  (void)lb_module_ptr;
}

void envoy_dynamic_module_on_cluster_lb_choose_host(
    envoy_dynamic_module_type_cluster_lb_module_ptr lb_module_ptr,
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_cluster_host_envoy_ptr* host_out,
    envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr* async_handle_out) {
  (void)lb_module_ptr;
  (void)context_envoy_ptr;
  *host_out = NULL;
  *async_handle_out = NULL;
}

void envoy_dynamic_module_on_cluster_worker_event(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr, uint64_t event_id) {
  (void)cluster_envoy_ptr;
  (void)cluster_module_ptr;
  atomic_fetch_add(&g_state.worker_event_count, 1);
  atomic_store(&g_state.last_event_id, event_id);
}

void envoy_dynamic_module_on_cluster_worker_slot_data_destroy(
    envoy_dynamic_module_type_cluster_worker_slot_data_module_ptr data_module_ptr) {
  atomic_fetch_add(&g_state.destroy_count, 1);
  if (data_module_ptr != NULL) {
    free(data_module_ptr);
  }
}

// Test-only accessors exported as ordinary functions; the test harness dlsym's them.
size_t cluster_run_on_all_workers_test_get_count(void) {
  return atomic_load(&g_state.worker_event_count);
}

uint64_t cluster_run_on_all_workers_test_get_last_event_id(void) {
  return atomic_load(&g_state.last_event_id);
}

size_t cluster_run_on_all_workers_test_get_destroy_count(void) {
  return atomic_load(&g_state.destroy_count);
}
