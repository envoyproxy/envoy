// An upstream bridge module that returns EndStream from encode_headers for testing the
// deferred local reply path via dispatcher().post().

#include <stddef.h>
#include <stdint.h>

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
  (void)bridge_envoy_ptr;
  (void)bridge_module_ptr;
  (void)end_of_stream;

  // Set a response body so the deferred local reply sends headers + data.
  const char* body = "access denied";
  envoy_dynamic_module_type_module_buffer body_buf = {.ptr = body, .length = 13};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_body(bridge_envoy_ptr,
                                                                          body_buf);

  const char* status_key = ":status";
  const char* status_val = "403";
  envoy_dynamic_module_type_module_buffer sk = {.ptr = status_key, .length = 7};
  envoy_dynamic_module_type_module_buffer sv = {.ptr = status_val, .length = 3};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_header(bridge_envoy_ptr, sk,
                                                                            sv);

  return envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status_EndStream;
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
}
