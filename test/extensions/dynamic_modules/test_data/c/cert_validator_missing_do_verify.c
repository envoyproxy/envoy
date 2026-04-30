// Cert validator fake that omits envoy_dynamic_module_on_cert_validator_do_verify_cert_chain. Used
// to exercise the matching symbol-resolution failure path in newDynamicModuleCertValidatorConfig.
#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

static int config_dummy = 0;

envoy_dynamic_module_type_cert_validator_config_module_ptr
envoy_dynamic_module_on_cert_validator_config_new(
    envoy_dynamic_module_type_cert_validator_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  return &config_dummy;
}

void envoy_dynamic_module_on_cert_validator_config_destroy(
    envoy_dynamic_module_type_cert_validator_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

// Intentionally NOT defining envoy_dynamic_module_on_cert_validator_do_verify_cert_chain.

int envoy_dynamic_module_on_cert_validator_get_ssl_verify_mode(
    envoy_dynamic_module_type_cert_validator_config_module_ptr config_module_ptr,
    bool handshaker_provides_certificates) {
  (void)config_module_ptr;
  (void)handshaker_provides_certificates;
  return 0;
}

void envoy_dynamic_module_on_cert_validator_update_digest(
    envoy_dynamic_module_type_cert_validator_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_module_buffer* out_data) {
  (void)config_module_ptr;
  out_data->ptr = NULL;
  out_data->length = 0;
}
