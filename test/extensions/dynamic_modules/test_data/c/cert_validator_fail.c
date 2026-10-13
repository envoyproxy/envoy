#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This is a cert validator module that rejects all certificates.

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

static const char error_msg[] = "certificate rejected by module";

envoy_dynamic_module_type_cert_validator_validation_result
envoy_dynamic_module_on_cert_validator_do_verify_cert_chain(
    envoy_dynamic_module_type_cert_validator_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_cert_validator_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer* certs, size_t certs_count,
    envoy_dynamic_module_type_envoy_buffer host_name, bool is_server) {
  (void)config_module_ptr;
  (void)certs;
  (void)certs_count;
  (void)host_name;
  (void)is_server;

  // Set error details via the callback.
  envoy_dynamic_module_type_module_buffer error_buf;
  error_buf.ptr = error_msg;
  error_buf.length = sizeof(error_msg) - 1;
  envoy_dynamic_module_callback_cert_validator_set_error_details(config_envoy_ptr, error_buf);

  envoy_dynamic_module_type_cert_validator_validation_result result;
  result.status = envoy_dynamic_module_type_cert_validator_validation_status_Failed;
  result.detailed_status =
      envoy_dynamic_module_type_cert_validator_client_validation_status_Failed;
  // SSL_AD_BAD_CERTIFICATE = 42.
  result.tls_alert = 42;
  result.has_tls_alert = true;
  return result;
}

int envoy_dynamic_module_on_cert_validator_get_ssl_verify_mode(
    envoy_dynamic_module_type_cert_validator_config_module_ptr config_module_ptr,
    bool handshaker_provides_certificates) {
  (void)config_module_ptr;
  (void)handshaker_provides_certificates;
  return 0x03;
}

static const char digest_data[] = "cert_validator_fail";

void envoy_dynamic_module_on_cert_validator_update_digest(
    envoy_dynamic_module_type_cert_validator_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_module_buffer* out_data) {
  (void)config_module_ptr;
  out_data->ptr = digest_data;
  out_data->length = sizeof(digest_data) - 1;
}

