#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// TLS certificate selector module whose selector creation fails, so every handshake fails.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

static int config_dummy = 0;

envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr
envoy_dynamic_module_on_tls_certificate_selector_config_new(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config,
    bool is_upstream) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  (void)is_upstream;
  return &config_dummy;
}

void envoy_dynamic_module_on_tls_certificate_selector_config_destroy(
    envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_tls_certificate_selector_module_ptr
envoy_dynamic_module_on_tls_certificate_selector_new(
    envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr config_module_ptr,
    const envoy_dynamic_module_type_envoy_buffer* certificates, size_t certificates_count) {
  (void)config_module_ptr;
  (void)certificates;
  (void)certificates_count;
  return NULL;
}

// Envoy never uses or destroys a selector that the module failed to create.

void envoy_dynamic_module_on_tls_certificate_selector_destroy(
    envoy_dynamic_module_type_tls_certificate_selector_module_ptr selector_module_ptr) {
  (void)selector_module_ptr;
  abort();
}

envoy_dynamic_module_type_tls_certificate_selector_status
envoy_dynamic_module_on_tls_certificate_selector_select(
    envoy_dynamic_module_type_tls_certificate_selector_module_ptr selector_module_ptr,
    envoy_dynamic_module_type_tls_certificate_selector_context_envoy_ptr context_envoy_ptr,
    size_t* certificate_index, envoy_dynamic_module_type_module_buffer* certificate_name) {
  (void)selector_module_ptr;
  (void)context_envoy_ptr;
  (void)certificate_index;
  (void)certificate_name;
  abort();
}
