// A no-op cluster dynamic module for testing purposes.

#include "source/extensions/clusters/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  // Return the ABI version.
  return kAbiVersion;
}

envoy_dynamic_module_type_cluster_config_module_ptr envoy_dynamic_module_on_cluster_config_new(
    envoy_dynamic_module_type_cluster_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  // Return a non-null pointer to indicate success.
  static int dummy_config = 1;
  return &dummy_config;
}

void envoy_dynamic_module_on_cluster_config_destroy(
    envoy_dynamic_module_type_cluster_config_module_ptr config_ptr) {
  (void)config_ptr;
  // No-op.
}

envoy_dynamic_module_type_cluster_module_ptr envoy_dynamic_module_on_cluster_new(
    envoy_dynamic_module_type_cluster_config_module_ptr config_ptr,
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  (void)config_ptr;
  (void)cluster_envoy_ptr;
  // Return a non-null pointer to indicate success.
  static int dummy_cluster = 1;
  return &dummy_cluster;
}

void envoy_dynamic_module_on_cluster_destroy(
    envoy_dynamic_module_type_cluster_module_ptr cluster_ptr) {
  (void)cluster_ptr;
  // No-op.
}

void envoy_dynamic_module_on_cluster_init(envoy_dynamic_module_type_cluster_module_ptr cluster_ptr,
                                          envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  (void)cluster_ptr;
  (void)cluster_envoy_ptr;
  // No-op. In a real module, you would call pre_init_complete here.
}

void envoy_dynamic_module_on_cluster_cleanup(
    envoy_dynamic_module_type_cluster_module_ptr cluster_ptr,
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  (void)cluster_ptr;
  (void)cluster_envoy_ptr;
  // No-op.
}

envoy_dynamic_module_type_load_balancer_module_ptr envoy_dynamic_module_on_load_balancer_new(
    envoy_dynamic_module_type_cluster_module_ptr cluster_ptr) {
  (void)cluster_ptr;
  // Return a non-null pointer to indicate success.
  static int dummy_lb = 1;
  return &dummy_lb;
}

void envoy_dynamic_module_on_load_balancer_destroy(
    envoy_dynamic_module_type_load_balancer_module_ptr lb_ptr) {
  (void)lb_ptr;
  // No-op.
}

envoy_dynamic_module_type_host_envoy_ptr envoy_dynamic_module_on_load_balancer_choose_host(
    envoy_dynamic_module_type_load_balancer_module_ptr lb_ptr,
    envoy_dynamic_module_type_lb_context_envoy_ptr context) {
  (void)lb_ptr;
  (void)context;
  // Return null to indicate no host selected.
  return 0;
}

void envoy_dynamic_module_on_host_set_change(
    envoy_dynamic_module_type_cluster_module_ptr cluster_ptr,
    envoy_dynamic_module_type_host_info* hosts_added, size_t hosts_added_size,
    envoy_dynamic_module_type_host_info* hosts_removed, size_t hosts_removed_size) {
  (void)cluster_ptr;
  (void)hosts_added;
  (void)hosts_added_size;
  (void)hosts_removed;
  (void)hosts_removed_size;
  // No-op.
}

void envoy_dynamic_module_on_host_health_change(
    envoy_dynamic_module_type_cluster_module_ptr cluster_ptr,
    envoy_dynamic_module_type_host_envoy_ptr host, envoy_dynamic_module_type_host_health health,
    envoy_dynamic_module_type_health_transition transition) {
  (void)cluster_ptr;
  (void)host;
  (void)health;
  (void)transition;
  // No-op.
}

