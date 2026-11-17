#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// TLS handshaker module whose per-connection handshaker creation fails, so Envoy closes the
// connection.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

static int config_dummy = 0;

envoy_dynamic_module_type_tls_handshaker_config_module_ptr
envoy_dynamic_module_on_tls_handshaker_config_new(
    envoy_dynamic_module_type_tls_handshaker_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config,
    envoy_dynamic_module_type_tls_handshaker_capabilities* capabilities) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  (void)capabilities;
  return &config_dummy;
}

void envoy_dynamic_module_on_tls_handshaker_config_destroy(
    envoy_dynamic_module_type_tls_handshaker_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_tls_handshaker_module_ptr envoy_dynamic_module_on_tls_handshaker_new(
    envoy_dynamic_module_type_tls_handshaker_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_tls_handshaker_ssl_ptr ssl) {
  (void)config_module_ptr;
  (void)ssl;
  return NULL;
}

void envoy_dynamic_module_on_tls_handshaker_destroy(
    envoy_dynamic_module_type_tls_handshaker_module_ptr handshaker_module_ptr) {
  (void)handshaker_module_ptr;
}

envoy_dynamic_module_type_tls_handshaker_result
envoy_dynamic_module_on_tls_handshaker_handshake(
    envoy_dynamic_module_type_tls_handshaker_module_ptr handshaker_module_ptr) {
  (void)handshaker_module_ptr;
  return envoy_dynamic_module_type_tls_handshaker_result_RunDefault;
}
