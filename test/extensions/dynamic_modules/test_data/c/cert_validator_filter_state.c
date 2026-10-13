#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This is a cert validator module that exercises the filter state callbacks during
// do_verify_cert_chain. It sets a key-value pair and reads it back to verify the round-trip.

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

static const char fs_key[] = "cert_validator.test_key";
static const char fs_value[] = "cert_validator.test_value";

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

  envoy_dynamic_module_type_cert_validator_validation_result result;

  // Set a filter state value.
  envoy_dynamic_module_type_module_buffer key_buf;
  key_buf.ptr = fs_key;
  key_buf.length = sizeof(fs_key) - 1;
  envoy_dynamic_module_type_module_buffer value_buf;
  value_buf.ptr = fs_value;
  value_buf.length = sizeof(fs_value) - 1;

  bool set_ok =
      envoy_dynamic_module_callback_cert_validator_set_filter_state(config_envoy_ptr, key_buf,
                                                                    value_buf);
  if (!set_ok) {
    // If set fails, return a failure result.
    result.status = envoy_dynamic_module_type_cert_validator_validation_status_Failed;
    result.detailed_status =
        envoy_dynamic_module_type_cert_validator_client_validation_status_Failed;
    result.tls_alert = 0;
    result.has_tls_alert = false;
    return result;
  }

  // Read the filter state value back.
  envoy_dynamic_module_type_envoy_buffer read_value;
  bool get_ok = envoy_dynamic_module_callback_cert_validator_get_filter_state(config_envoy_ptr,
                                                                              key_buf, &read_value);
  if (!get_ok) {
    result.status = envoy_dynamic_module_type_cert_validator_validation_status_Failed;
    result.detailed_status =
        envoy_dynamic_module_type_cert_validator_client_validation_status_Failed;
    result.tls_alert = 0;
    result.has_tls_alert = false;
    return result;
  }

  // Verify the value matches.
  if (read_value.length != sizeof(fs_value) - 1 ||
      memcmp(read_value.ptr, fs_value, read_value.length) != 0) {
    result.status = envoy_dynamic_module_type_cert_validator_validation_status_Failed;
    result.detailed_status =
        envoy_dynamic_module_type_cert_validator_client_validation_status_Failed;
    result.tls_alert = 0;
    result.has_tls_alert = false;
    return result;
  }

  result.status = envoy_dynamic_module_type_cert_validator_validation_status_Successful;
  result.detailed_status =
      envoy_dynamic_module_type_cert_validator_client_validation_status_Validated;
  result.tls_alert = 0;
  result.has_tls_alert = false;
  return result;
}

int envoy_dynamic_module_on_cert_validator_get_ssl_verify_mode(
    envoy_dynamic_module_type_cert_validator_config_module_ptr config_module_ptr,
    bool handshaker_provides_certificates) {
  (void)config_module_ptr;
  (void)handshaker_provides_certificates;
  // SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT = 0x01 | 0x02 = 0x03.
  return 0x03;
}

static const char digest_data[] = "cert_validator_filter_state";

void envoy_dynamic_module_on_cert_validator_update_digest(
    envoy_dynamic_module_type_cert_validator_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_module_buffer* out_data) {
  (void)config_module_ptr;
  out_data->ptr = digest_data;
  out_data->length = sizeof(digest_data) - 1;
}

