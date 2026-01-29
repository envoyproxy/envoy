#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/abi/abi_version.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return kAbiVersion;
}

// A bootstrap extension that is missing
// envoy_dynamic_module_on_bootstrap_extension_server_initialized.

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
  return (envoy_dynamic_module_type_bootstrap_extension_module_ptr)0x2;
}

// envoy_dynamic_module_on_bootstrap_extension_server_initialized is intentionally missing.

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

void envoy_dynamic_module_on_bootstrap_extension_http_callout_done(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    uint64_t callout_id, envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_module_ptr;
  (void)callout_id;
  (void)result;
  (void)headers;
  (void)headers_size;
  (void)body_chunks;
  (void)body_chunks_size;
}
