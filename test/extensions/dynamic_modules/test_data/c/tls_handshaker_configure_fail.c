#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// TLS handshaker module whose SSL_CTX configuration hook fails.

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

bool envoy_dynamic_module_on_tls_handshaker_configure_ssl_context(
    envoy_dynamic_module_type_tls_handshaker_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_tls_handshaker_ssl_ctx_ptr ssl_ctx) {
  (void)config_module_ptr;
  (void)ssl_ctx;
  return false;
}
