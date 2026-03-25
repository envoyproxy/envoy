// An upstream bridge module that sends headers with end_stream=true and no body from encode_headers.
// Tests the deferred local reply path where headers are sent with end_stream=true.

#include <stdbool.h>
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

void envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_module_ptr;
  (void)end_of_stream;
  envoy_dynamic_module_type_module_buffer empty_body = {.ptr = NULL, .length = 0};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(bridge_envoy_ptr, 200, NULL,
                                                                       0, empty_body);
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
}
