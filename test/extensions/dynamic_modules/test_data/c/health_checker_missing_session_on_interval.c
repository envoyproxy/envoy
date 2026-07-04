#include "source/extensions/dynamic_modules/abi/abi.h"

static int config_dummy = 0;
static int session_dummy = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_health_checker_config_module_ptr
envoy_dynamic_module_on_health_checker_config_new(
    envoy_dynamic_module_type_health_checker_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  return &config_dummy;
}

void envoy_dynamic_module_on_health_checker_config_destroy(
    envoy_dynamic_module_type_health_checker_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_health_checker_session_module_ptr
envoy_dynamic_module_on_health_checker_session_new(
    envoy_dynamic_module_type_health_checker_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_health_checker_session_envoy_ptr session_envoy_ptr) {
  (void)config_module_ptr;
  (void)session_envoy_ptr;
  return &session_dummy;
}
void envoy_dynamic_module_on_health_checker_session_destroy(
    envoy_dynamic_module_type_health_checker_session_module_ptr session_module_ptr) {
  (void)session_module_ptr;
}
