// Stub implementations of the transport socket ABI for testing purposes.
// These functions are normally provided by the loaded dynamic module, but for
// unit tests that don't load a real module, we need to provide stubs.
// NOLINT(namespace-envoy)

#include <cstring>

#include "source/extensions/dynamic_modules/transport_socket_abi.h"

extern "C" {

// Config lifecycle stubs
envoy_dynamic_module_type_transport_socket_config_module_ptr
envoy_dynamic_module_on_transport_socket_config_new(
    envoy_dynamic_module_type_transport_socket_config_envoy_ptr /* envoy_config */,
    const char* /* socket_name */, size_t /* socket_name_length */, const char* /* socket_config */,
    size_t /* socket_config_length */, bool /* is_upstream */) {
  // Return a dummy pointer for testing
  static int dummy_config = 42;
  return &dummy_config;
}

void envoy_dynamic_module_on_transport_socket_config_destroy(
    envoy_dynamic_module_type_transport_socket_config_module_ptr /* module_config */) {
  // No-op for stub
}

// Socket lifecycle stubs
envoy_dynamic_module_type_transport_socket_module_ptr envoy_dynamic_module_on_transport_socket_new(
    envoy_dynamic_module_type_transport_socket_config_module_ptr /* module_config */,
    envoy_dynamic_module_type_transport_socket_envoy_ptr /* envoy_socket */) {
  // Return a dummy pointer for testing
  static int dummy_socket = 42;
  return &dummy_socket;
}

void envoy_dynamic_module_on_transport_socket_destroy(
    envoy_dynamic_module_type_transport_socket_module_ptr /* module_socket */) {
  // No-op for stub
}

void envoy_dynamic_module_on_transport_socket_set_callbacks(
    envoy_dynamic_module_type_transport_socket_envoy_ptr /* envoy_socket */,
    envoy_dynamic_module_type_transport_socket_module_ptr /* module_socket */) {
  // No-op for stub
}

// Socket operations stubs
void envoy_dynamic_module_on_transport_socket_protocol(
    envoy_dynamic_module_type_transport_socket_module_ptr /* module_socket */,
    const char** result_ptr, size_t* result_length) {
  static const char* protocol = "stub";
  *result_ptr = protocol;
  *result_length = 4;
}

void envoy_dynamic_module_on_transport_socket_failure_reason(
    envoy_dynamic_module_type_transport_socket_module_ptr /* module_socket */,
    const char** result_ptr, size_t* result_length) {
  *result_ptr = nullptr;
  *result_length = 0;
}

bool envoy_dynamic_module_on_transport_socket_can_flush_close(
    envoy_dynamic_module_type_transport_socket_module_ptr /* module_socket */) {
  return true;
}

void envoy_dynamic_module_on_transport_socket_close(
    envoy_dynamic_module_type_transport_socket_module_ptr /* module_socket */,
    envoy_dynamic_module_type_connection_event /* event */) {
  // No-op for stub
}

void envoy_dynamic_module_on_transport_socket_on_connected(
    envoy_dynamic_module_type_transport_socket_envoy_ptr /* envoy_socket */,
    envoy_dynamic_module_type_transport_socket_module_ptr /* module_socket */) {
  // No-op for stub
}

envoy_dynamic_module_type_io_result envoy_dynamic_module_on_transport_socket_do_read(
    envoy_dynamic_module_type_transport_socket_envoy_ptr /* envoy_socket */,
    envoy_dynamic_module_type_transport_socket_module_ptr /* module_socket */,
    void* /* buffer_ptr */, size_t /* buffer_capacity */) {
  // Return success with 0 bytes read for stub
  envoy_dynamic_module_type_io_result result;
  result.action = envoy_dynamic_module_type_post_io_action_KeepOpen;
  result.bytes_processed = 0;
  result.end_stream_read = false;
  return result;
}

envoy_dynamic_module_type_io_result envoy_dynamic_module_on_transport_socket_do_write(
    envoy_dynamic_module_type_transport_socket_envoy_ptr /* envoy_socket */,
    envoy_dynamic_module_type_transport_socket_module_ptr /* module_socket */,
    const void* /* buffer_ptr */, size_t /* buffer_length */, bool /* end_stream */) {
  // Return success with 0 bytes written for stub
  envoy_dynamic_module_type_io_result result;
  result.action = envoy_dynamic_module_type_post_io_action_KeepOpen;
  result.bytes_processed = 0;
  result.end_stream_read = false;
  return result;
}

bool envoy_dynamic_module_on_transport_socket_get_ssl_info(
    envoy_dynamic_module_type_transport_socket_module_ptr /* module_socket */,
    envoy_dynamic_module_type_ssl_connection_info* info) {
  // Return empty SSL info for stub
  std::memset(info, 0, sizeof(*info));
  info->peer_certificate_validated = false;
  return false; // No SSL info available
}

bool envoy_dynamic_module_on_transport_socket_start_secure_transport(
    envoy_dynamic_module_type_transport_socket_envoy_ptr /* envoy_socket */,
    envoy_dynamic_module_type_transport_socket_module_ptr /* module_socket */) {
  // Return false indicating secure transport not started in stub
  return false;
}

void envoy_dynamic_module_on_transport_socket_configure_initial_congestion_window(
    envoy_dynamic_module_type_transport_socket_module_ptr /* module_socket */,
    uint64_t /* bandwidth_bits_per_sec */, uint64_t /* rtt_us */) {
  // No-op for stub
}

} // extern "C"
