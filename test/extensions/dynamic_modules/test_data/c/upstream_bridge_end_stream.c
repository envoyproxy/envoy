// An upstream bridge module that sends responses from encode_data, encode_trailers, and
// on_upstream_data for testing the local reply and response end-of-stream paths. Also exercises
// various ABI callbacks for request header and buffer operations.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr
envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  return (envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr)0x1;
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_config_destroy(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr
envoy_dynamic_module_on_upstream_http_tcp_bridge_new(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr) {
  (void)config_module_ptr;
  (void)bridge_envoy_ptr;
  return (envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr)0x2;
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_module_ptr;
  (void)end_of_stream;

  // Exercise request header ABI callbacks.
  size_t num_headers =
      envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size(
          bridge_envoy_ptr);
  if (num_headers > 0) {
    envoy_dynamic_module_type_envoy_http_header headers[16];
    envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers(bridge_envoy_ptr,
                                                                              headers);
  }

  // Exercise get_request_buffer with new envoy_buffer* signature.
  envoy_dynamic_module_type_envoy_buffer result_buffer;
  size_t result_buffer_length = 0;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer(bridge_envoy_ptr,
                                                                            &result_buffer,
                                                                            &result_buffer_length);
  (void)result_buffer;
  (void)result_buffer_length;
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_module_ptr;
  (void)end_of_stream;
  const char* body = "end_stream_body";
  envoy_dynamic_module_type_module_buffer body_buf = {.ptr = body, .length = 15};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(bridge_envoy_ptr, 403, NULL,
                                                                       0, body_buf);
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_trailers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr) {
  (void)bridge_module_ptr;
  envoy_dynamic_module_type_module_buffer empty_body = {.ptr = NULL, .length = 0};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(bridge_envoy_ptr, 200, NULL,
                                                                       0, empty_body);
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_module_ptr;
  (void)end_of_stream;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_headers(bridge_envoy_ptr,
                                                                                200, NULL, 0,
                                                                                false);
  envoy_dynamic_module_type_module_buffer empty_body = {.ptr = NULL, .length = 0};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_data(bridge_envoy_ptr,
                                                                            empty_body, true);
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr) {
  (void)bridge_module_ptr;
}
