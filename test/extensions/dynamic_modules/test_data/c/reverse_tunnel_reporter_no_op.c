#include "source/extensions/dynamic_modules/abi/abi.h"

// Minimal reverse tunnel reporter dynamic module: defines all five reporter
// ABI hooks plus program_init. Used by unit tests for the C++ factory.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_reverse_tunnel_reporter_module_ptr
envoy_dynamic_module_on_reverse_tunnel_reporter_new(
    envoy_dynamic_module_type_envoy_buffer reporter_config) {
  (void)reporter_config;
  static int reporter_dummy = 0;
  return &reporter_dummy;
}

void envoy_dynamic_module_on_reverse_tunnel_reporter_destroy(
    envoy_dynamic_module_type_reverse_tunnel_reporter_module_ptr reporter_ptr) {
  (void)reporter_ptr;
}

void envoy_dynamic_module_on_reverse_tunnel_server_initialized(
    envoy_dynamic_module_type_reverse_tunnel_reporter_module_ptr reporter_ptr) {
  (void)reporter_ptr;
}

void envoy_dynamic_module_on_reverse_tunnel_connected(
    envoy_dynamic_module_type_reverse_tunnel_reporter_module_ptr reporter_ptr,
    envoy_dynamic_module_type_envoy_buffer node_id,
    envoy_dynamic_module_type_envoy_buffer cluster_id,
    envoy_dynamic_module_type_envoy_buffer tenant_id) {
  (void)reporter_ptr;
  (void)node_id;
  (void)cluster_id;
  (void)tenant_id;
}

void envoy_dynamic_module_on_reverse_tunnel_disconnected(
    envoy_dynamic_module_type_reverse_tunnel_reporter_module_ptr reporter_ptr,
    envoy_dynamic_module_type_envoy_buffer node_id,
    envoy_dynamic_module_type_envoy_buffer cluster_id) {
  (void)reporter_ptr;
  (void)node_id;
  (void)cluster_id;
}
