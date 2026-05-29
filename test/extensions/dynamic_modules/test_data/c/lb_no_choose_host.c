// Test module that is missing the required on_lb_choose_host symbol.
// This is used to test the symbol resolution failure path.

#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

static int config_marker = 0;
static int lb_marker = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_lb_config_module_ptr envoy_dynamic_module_on_lb_config_new(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)lb_config_envoy_ptr;
  (void)name;
  (void)config;
  return &config_marker;
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
  return &lb_marker;
}

// Note: on_lb_choose_host is intentionally missing to test symbol resolution failure.

void envoy_dynamic_module_on_lb_destroy(envoy_dynamic_module_type_lb_module_ptr lb_module_ptr) {
  (void)lb_module_ptr;
}

