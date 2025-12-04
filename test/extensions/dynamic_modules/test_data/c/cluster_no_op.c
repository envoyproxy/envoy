#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

// No-op cluster module for testing. This implements the minimum required cluster ABI functions
// to verify the integration works correctly.

const char* envoy_dynamic_module_on_program_init() { return kAbiVersion; }

// Cluster config functions.
envoy_dynamic_module_type_cluster_config_module_ptr envoy_dynamic_module_on_cluster_config_new(
    envoy_dynamic_module_type_cluster_config_envoy_ptr config_envoy_ptr, 
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length,
    envoy_dynamic_module_type_buffer_module_ptr config, size_t config_length) {
  // Return a dummy pointer to indicate success.
  return (void*)1;
}

void envoy_dynamic_module_on_cluster_config_destroy(
    envoy_dynamic_module_type_cluster_config_module_ptr config_module_ptr) {
  // No-op.
}

// Cluster instance functions.
envoy_dynamic_module_type_cluster_module_ptr envoy_dynamic_module_on_cluster_new(
    envoy_dynamic_module_type_cluster_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  // Return a dummy pointer to indicate success.
  return (void*)2;
}

void envoy_dynamic_module_on_cluster_destroy(
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr) {
  // No-op.
}

void envoy_dynamic_module_on_cluster_init(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr) {
  // No-op: in a real module, you would call
  // envoy_dynamic_module_callback_cluster_pre_init_complete(cluster_envoy_ptr)
  // when initialization is done. For testing, Envoy will handle this gracefully.
  (void)cluster_envoy_ptr;
  (void)cluster_module_ptr;
}

void envoy_dynamic_module_on_cluster_cleanup(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr) {
  // No-op cleanup.
}

// Load balancer functions.
envoy_dynamic_module_type_load_balancer_module_ptr envoy_dynamic_module_on_load_balancer_new(
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_load_balancer_envoy_ptr lb_envoy_ptr) {
  // Return a dummy pointer to indicate success.
  return (void*)3;
}

void envoy_dynamic_module_on_load_balancer_destroy(
    envoy_dynamic_module_type_load_balancer_module_ptr lb_module_ptr) {
  // No-op.
}

envoy_dynamic_module_type_host_envoy_ptr envoy_dynamic_module_on_load_balancer_choose_host(
    envoy_dynamic_module_type_load_balancer_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_load_balancer_module_ptr lb_module_ptr,
    envoy_dynamic_module_type_lb_context_envoy_ptr context) {
  // Return NULL to indicate no host available (cluster is empty).
  return NULL;
}

// Host set change callback.
void envoy_dynamic_module_on_host_set_change(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_host_envoy_ptr* hosts_added, size_t hosts_added_count,
    envoy_dynamic_module_type_host_envoy_ptr* hosts_removed, size_t hosts_removed_count) {
  // No-op.
  (void)cluster_envoy_ptr;
  (void)cluster_module_ptr;
  (void)hosts_added;
  (void)hosts_added_count;
  (void)hosts_removed;
  (void)hosts_removed_count;
}

// Health check change callback.
void envoy_dynamic_module_on_host_health_change(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_host_envoy_ptr host,
    envoy_dynamic_module_type_health_transition transition,
    envoy_dynamic_module_type_host_health current_health) {
  // No-op.
  (void)cluster_envoy_ptr;
  (void)cluster_module_ptr;
  (void)host;
  (void)transition;
  (void)current_health;
}
