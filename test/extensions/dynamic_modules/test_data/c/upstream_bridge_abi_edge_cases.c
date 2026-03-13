// An upstream bridge module that exercises ABI callback edge cases:
// - Getting headers/buffer before encodeHeaders sets them (null headers).
// - Requesting a header index beyond the available values.
// - send_response_headers and send_response_data with the new API.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

static envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr saved_bridge_ptr = NULL;

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
  saved_bridge_ptr = bridge_envoy_ptr;

  // Exercise ABI callbacks when request_headers_ is null (before encodeHeaders).
  envoy_dynamic_module_type_envoy_buffer result;
  size_t total = 0;
  const char* key = ":method";
  envoy_dynamic_module_type_module_buffer key_buf = {.ptr = key, .length = 7};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
      bridge_envoy_ptr, key_buf, &result, 0, &total);

  size_t size =
      envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size(
          bridge_envoy_ptr);
  (void)size;

  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers(bridge_envoy_ptr,
                                                                            NULL);

  // Exercise get_request_buffer with new envoy_buffer* signature when the request buffer is empty.
  envoy_dynamic_module_type_envoy_buffer req_buf;
  size_t req_buf_len = 0;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer(bridge_envoy_ptr,
                                                                             &req_buf, &req_buf_len);

  // Exercise get_response_buffer with new envoy_buffer* signature when response_buffer_ is null.
  envoy_dynamic_module_type_envoy_buffer resp_buf;
  size_t resp_buf_len = 0;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer(bridge_envoy_ptr,
                                                                            &resp_buf,
                                                                            &resp_buf_len);

  return (envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr)0x2;
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_module_ptr;
  (void)end_of_stream;

  // Exercise get_request_header with an out-of-range index.
  envoy_dynamic_module_type_envoy_buffer result;
  size_t total = 0;
  const char* key = ":method";
  envoy_dynamic_module_type_module_buffer key_buf = {.ptr = key, .length = 7};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
      bridge_envoy_ptr, key_buf, &result, 999, &total);

  // Exercise get_request_header without total_count_out.
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
      bridge_envoy_ptr, key_buf, &result, 0, NULL);

  // Exercise get_request_headers_size when headers are available.
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size(bridge_envoy_ptr);

  // Exercise send_response_headers and send_response_data with the new API.
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_headers(bridge_envoy_ptr,
                                                                               200, NULL, 0, false);
  const char* body = "body";
  envoy_dynamic_module_type_module_buffer body_buf = {.ptr = body, .length = 4};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_data(bridge_envoy_ptr,
                                                                             body_buf, true);
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_envoy_ptr;
  (void)bridge_module_ptr;
  (void)end_of_stream;
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_trailers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr) {
  (void)bridge_envoy_ptr;
  (void)bridge_module_ptr;
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_envoy_ptr;
  (void)bridge_module_ptr;
  (void)end_of_stream;
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr) {
  (void)bridge_module_ptr;
  saved_bridge_ptr = NULL;
}
