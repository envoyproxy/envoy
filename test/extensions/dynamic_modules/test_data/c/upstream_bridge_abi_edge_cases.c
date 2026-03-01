// An upstream bridge module that exercises ABI callback edge cases:
// - Getting headers/buffer before encodeHeaders sets them (null headers).
// - Requesting a header index beyond the available values.
// - Setting response headers/body with null headers pointer.
// - Appending empty response body.

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

  // Exercise get_request_buffer when the request buffer is empty (before any data is set).
  uintptr_t buf_ptr = 0;
  size_t buf_len = 0;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer(bridge_envoy_ptr,
                                                                           &buf_ptr, &buf_len);

  // Exercise get_response_buffer when response_buffer_ is null (before onUpstreamData).
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer(bridge_envoy_ptr,
                                                                            &buf_ptr, &buf_len);

  // Exercise set/add response header when response_headers_ is null (before encodeHeaders
  // initializes them).
  const char* hk = ":status";
  const char* hv = "200";
  envoy_dynamic_module_type_module_buffer hk_buf = {.ptr = hk, .length = 7};
  envoy_dynamic_module_type_module_buffer hv_buf = {.ptr = hv, .length = 3};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_header(bridge_envoy_ptr,
                                                                            hk_buf, hv_buf);
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_add_response_header(bridge_envoy_ptr,
                                                                            hk_buf, hv_buf);

  return (envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr)0x2;
}

envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status
envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers(
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

  // Exercise set_response_header to cover the non-null headers path.
  const char* hk = ":status";
  const char* hv = "200";
  envoy_dynamic_module_type_module_buffer hk_buf = {.ptr = hk, .length = 7};
  envoy_dynamic_module_type_module_buffer hv_buf = {.ptr = hv, .length = 3};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_header(bridge_envoy_ptr,
                                                                            hk_buf, hv_buf);

  // Exercise add_response_header.
  const char* ak = "x-custom";
  const char* av = "val";
  envoy_dynamic_module_type_module_buffer ak_buf = {.ptr = ak, .length = 8};
  envoy_dynamic_module_type_module_buffer av_buf = {.ptr = av, .length = 3};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_add_response_header(bridge_envoy_ptr,
                                                                            ak_buf, av_buf);

  // Exercise append_response_body with non-empty data.
  const char* body = "body";
  envoy_dynamic_module_type_module_buffer body_buf = {.ptr = body, .length = 4};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_append_response_body(bridge_envoy_ptr,
                                                                             body_buf);

  // Exercise append_response_body with empty data.
  envoy_dynamic_module_type_module_buffer empty_buf = {.ptr = NULL, .length = 0};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_append_response_body(bridge_envoy_ptr,
                                                                             empty_buf);

  // Exercise set_response_body with empty data.
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_body(bridge_envoy_ptr,
                                                                          empty_buf);

  // Exercise append_request_buffer with empty data.
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_append_request_buffer(bridge_envoy_ptr,
                                                                              empty_buf);

  // Exercise add_response_trailer when trailers have not been created yet (nullptr).
  // This covers the trailer creation path inside add_response_trailer.
  const char* tk = "x-trail";
  const char* tv = "val";
  envoy_dynamic_module_type_module_buffer tk_buf = {.ptr = tk, .length = 7};
  envoy_dynamic_module_type_module_buffer tv_buf = {.ptr = tv, .length = 3};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_add_response_trailer(bridge_envoy_ptr,
                                                                             tk_buf, tv_buf);

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
  return envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status_Continue;
}

envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status
envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_trailers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr) {
  (void)bridge_envoy_ptr;
  (void)bridge_module_ptr;
  return envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status_Continue;
}

envoy_dynamic_module_type_on_upstream_http_tcp_bridge_on_upstream_data_status
envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_envoy_ptr;
  (void)bridge_module_ptr;
  (void)end_of_stream;
  return envoy_dynamic_module_type_on_upstream_http_tcp_bridge_on_upstream_data_status_Continue;
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr) {
  (void)bridge_module_ptr;
  saved_bridge_ptr = NULL;
}
