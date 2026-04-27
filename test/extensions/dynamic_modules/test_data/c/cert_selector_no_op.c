#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// Minimal cert selector that always returns Failed. Exists to exercise the ABI contract
// (symbol resolution + lifecycle hooks) without depending on certificate material.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

static int config_dummy = 0;
static int selector_dummy = 0;

envoy_dynamic_module_type_cert_selector_config_module_ptr
envoy_dynamic_module_on_cert_selector_config_new(
    envoy_dynamic_module_type_cert_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  return &config_dummy;
}

void envoy_dynamic_module_on_cert_selector_config_destroy(
    envoy_dynamic_module_type_cert_selector_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_cert_selector_module_ptr envoy_dynamic_module_on_cert_selector_new(
    envoy_dynamic_module_type_cert_selector_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_cert_selector_envoy_ptr selector_envoy_ptr,
    size_t num_pre_provisioned_contexts) {
  (void)config_module_ptr;
  (void)selector_envoy_ptr;
  (void)num_pre_provisioned_contexts;
  return &selector_dummy;
}

void envoy_dynamic_module_on_cert_selector_destroy(
    envoy_dynamic_module_type_cert_selector_module_ptr selector_module_ptr) {
  (void)selector_module_ptr;
}

envoy_dynamic_module_type_cert_selector_result envoy_dynamic_module_on_cert_selector_select(
    envoy_dynamic_module_type_cert_selector_envoy_ptr selector_envoy_ptr,
    envoy_dynamic_module_type_cert_selector_module_ptr selector_module_ptr) {
  (void)selector_envoy_ptr;
  (void)selector_module_ptr;
  envoy_dynamic_module_type_cert_selector_result r;
  r.status = envoy_dynamic_module_type_cert_selector_result_Failed;
  r.context_index = 0;
  r.cert_chain_pem.ptr = NULL;
  r.cert_chain_pem.length = 0;
  r.private_key_pem.ptr = NULL;
  r.private_key_pem.length = 0;
  r.cache_key.ptr = NULL;
  r.cache_key.length = 0;
  r.staple_ocsp = false;
  return r;
}
