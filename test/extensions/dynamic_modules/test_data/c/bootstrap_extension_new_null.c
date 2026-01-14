#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return kAbiVersion;
}

// A bootstrap extension where envoy_dynamic_module_on_bootstrap_extension_new returns nullptr.

envoy_dynamic_module_type_bootstrap_extension_config_module_ptr
envoy_dynamic_module_on_bootstrap_extension_config_new(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)extension_config_envoy_ptr;
  (void)name;
  (void)config;
  return (envoy_dynamic_module_type_bootstrap_extension_config_module_ptr)0x1;
}

void envoy_dynamic_module_on_bootstrap_extension_config_destroy(
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_ptr) {
  (void)extension_config_ptr;
}

envoy_dynamic_module_type_bootstrap_extension_module_ptr
envoy_dynamic_module_on_bootstrap_extension_new(
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_ptr,
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr) {
  (void)extension_config_ptr;
  (void)extension_envoy_ptr;
  // Return nullptr to test null extension handling.
  return NULL;
}

void envoy_dynamic_module_on_bootstrap_extension_server_initialized(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr) {
  (void)extension_envoy_ptr;
  (void)extension_module_ptr;
}

void envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr) {
  (void)extension_envoy_ptr;
  (void)extension_module_ptr;
}

void envoy_dynamic_module_on_bootstrap_extension_destroy(
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr) {
  (void)extension_module_ptr;
}

void envoy_dynamic_module_on_bootstrap_extension_config_scheduled(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_ptr,
    uint64_t event_id) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_ptr;
  (void)event_id;
}
