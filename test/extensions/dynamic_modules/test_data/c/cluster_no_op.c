#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"


envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

// A simple no-op cluster module for testing.

envoy_dynamic_module_type_cluster_config_module_ptr envoy_dynamic_module_on_cluster_config_new(
    envoy_dynamic_module_type_cluster_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  // Return a dummy pointer.
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
  // Return a dummy pointer.
  return (envoy_dynamic_module_type_cluster_module_ptr)0x2;
}

void envoy_dynamic_module_on_cluster_init(
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  (void)cluster_module_ptr;
  (void)cluster_envoy_ptr;
  // The C++ test code will call preInitComplete and addHosts directly.
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
  // Return a dummy pointer.
  return (envoy_dynamic_module_type_cluster_lb_module_ptr)0x3;
}

void envoy_dynamic_module_on_cluster_lb_destroy(
    envoy_dynamic_module_type_cluster_lb_module_ptr lb_module_ptr) {
  (void)lb_module_ptr;
}

envoy_dynamic_module_type_cluster_host_envoy_ptr envoy_dynamic_module_on_cluster_lb_choose_host(
    envoy_dynamic_module_type_cluster_lb_module_ptr lb_module_ptr,
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr) {
  (void)lb_module_ptr;
  (void)context_envoy_ptr;
  // Return NULL. The C++ test will drive host selection.
  return NULL;
}
