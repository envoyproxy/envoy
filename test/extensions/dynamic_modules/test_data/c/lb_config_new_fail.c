#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// Load balancer module that fails to create config.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_lb_config_module_ptr envoy_dynamic_module_on_lb_config_new(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)lb_config_envoy_ptr;
  (void)name;
  (void)config;
  // Return null to indicate failure.
  return NULL;
}

void envoy_dynamic_module_on_lb_config_destroy(
    envoy_dynamic_module_type_lb_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_lb_module_ptr
envoy_dynamic_module_on_lb_new(envoy_dynamic_module_type_lb_config_module_ptr config_module_ptr,
                               envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr) {
  (void)config_module_ptr;
  (void)lb_envoy_ptr;
  return NULL;
}

int64_t
envoy_dynamic_module_on_lb_choose_host(envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
                                       envoy_dynamic_module_type_lb_module_ptr lb_module_ptr,
                                       envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr) {
  (void)lb_envoy_ptr;
  (void)lb_module_ptr;
  (void)context_envoy_ptr;
  return -1;
}

void envoy_dynamic_module_on_lb_destroy(envoy_dynamic_module_type_lb_module_ptr lb_module_ptr) {
  (void)lb_module_ptr;
}

