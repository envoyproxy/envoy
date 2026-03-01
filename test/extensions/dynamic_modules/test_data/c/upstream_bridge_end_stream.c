// An upstream bridge module that returns EndStream from encode_data, encode_trailers, and
// on_upstream_data for testing the local reply and response end-of-stream paths. Also exercises
// various ABI callbacks for request/response header, buffer, and trailer manipulation.

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

envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status
envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers(
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

  // Exercise request buffer ABI callbacks.
  // First set the request buffer so it is non-empty, then read it back via get_request_buffer
  // to cover the linearize + getRawSlices path (abi_impl.cc lines 93-103).
  const char* new_data = "replaced";
  envoy_dynamic_module_type_module_buffer mod_buf = {.ptr = new_data, .length = 8};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_request_buffer(bridge_envoy_ptr,
                                                                           mod_buf);

  uintptr_t buf_ptr = 0;
  size_t buf_len = 0;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer(bridge_envoy_ptr,
                                                                           &buf_ptr, &buf_len);

  envoy_dynamic_module_callback_upstream_http_tcp_bridge_drain_request_buffer(bridge_envoy_ptr, 3);

  // Exercise response trailer ABI callbacks.
  const char* trailer_key = "x-trailer";
  const char* trailer_val = "value";
  envoy_dynamic_module_type_module_buffer tk = {.ptr = trailer_key, .length = 9};
  envoy_dynamic_module_type_module_buffer tv = {.ptr = trailer_val, .length = 5};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_trailer(bridge_envoy_ptr, tk,
                                                                             tv);
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_add_response_trailer(bridge_envoy_ptr, tk,
                                                                             tv);

  return envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status_Continue;
}

envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status
envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_envoy_ptr;
  (void)bridge_module_ptr;
  (void)end_of_stream;
  // Set response body and return EndStream to trigger sendLocalReply.
  const char* body = "end_stream_body";
  envoy_dynamic_module_type_module_buffer body_buf = {.ptr = body, .length = 15};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_body(bridge_envoy_ptr,
                                                                          body_buf);

  const char* status_key = ":status";
  const char* status_val = "403";
  envoy_dynamic_module_type_module_buffer sk = {.ptr = status_key, .length = 7};
  envoy_dynamic_module_type_module_buffer sv = {.ptr = status_val, .length = 3};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_header(bridge_envoy_ptr, sk,
                                                                            sv);

  return envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status_EndStream;
}

envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status
envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_trailers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr) {
  (void)bridge_envoy_ptr;
  (void)bridge_module_ptr;
  return envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status_EndStream;
}

envoy_dynamic_module_type_on_upstream_http_tcp_bridge_on_upstream_data_status
envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_envoy_ptr;
  (void)bridge_module_ptr;
  (void)end_of_stream;
  return envoy_dynamic_module_type_on_upstream_http_tcp_bridge_on_upstream_data_status_EndStream;
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr) {
  (void)bridge_module_ptr;
}
