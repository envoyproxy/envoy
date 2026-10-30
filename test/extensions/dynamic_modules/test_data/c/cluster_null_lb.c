#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// A cluster module whose on_cluster_lb_new returns null while still exporting the host membership
// update hook. Used to verify the host reads a null load balancer as a configuration error and
// never registers the membership callback against it.

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
  return (envoy_dynamic_module_type_cluster_module_ptr)0x2;
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

// Reports a configuration error by returning null.
envoy_dynamic_module_type_cluster_lb_module_ptr envoy_dynamic_module_on_cluster_lb_new(
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr) {
  (void)cluster_module_ptr;
  (void)lb_envoy_ptr;
  return NULL;
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

// Exported so the host resolves the hook, but it must never run because the load balancer is null.
void envoy_dynamic_module_on_cluster_lb_on_host_membership_update(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_cluster_lb_module_ptr lb_module_ptr, size_t num_hosts_added,
    size_t num_hosts_removed) {
  (void)lb_envoy_ptr;
  (void)lb_module_ptr;
  (void)num_hosts_added;
  (void)num_hosts_removed;
}

void envoy_dynamic_module_on_cluster_server_initialized(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr) {
  (void)cluster_envoy_ptr;
  (void)cluster_module_ptr;
}

void envoy_dynamic_module_on_cluster_drain_started(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr) {
  (void)cluster_envoy_ptr;
  (void)cluster_module_ptr;
}

void envoy_dynamic_module_on_cluster_shutdown(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_event_cb completion_callback, void* completion_context) {
  (void)cluster_envoy_ptr;
  (void)cluster_module_ptr;
  completion_callback(completion_context);
}

void envoy_dynamic_module_on_cluster_http_callout_done(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size) {
  (void)cluster_envoy_ptr;
  (void)cluster_module_ptr;
  (void)callout_id;
  (void)result;
  (void)headers;
  (void)headers_size;
  (void)body_chunks;
  (void)body_chunks_size;
}
