#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// TLS certificate selector module that selects the index right past the last certificate.

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
  size_t* count = malloc(sizeof(size_t));
  *count = certificates_count;
  return count;
}

void envoy_dynamic_module_on_tls_certificate_selector_destroy(
    envoy_dynamic_module_type_tls_certificate_selector_module_ptr selector_module_ptr) {
  free((void*)selector_module_ptr);
}

envoy_dynamic_module_type_tls_certificate_selector_status
envoy_dynamic_module_on_tls_certificate_selector_select(
    envoy_dynamic_module_type_tls_certificate_selector_module_ptr selector_module_ptr,
    envoy_dynamic_module_type_tls_certificate_selector_context_envoy_ptr context_envoy_ptr,
    size_t* certificate_index, envoy_dynamic_module_type_module_buffer* certificate_name) {
  (void)certificate_name;
  (void)context_envoy_ptr;
  *certificate_index = *(const size_t*)selector_module_ptr;
  return envoy_dynamic_module_type_tls_certificate_selector_status_Configured;
}
