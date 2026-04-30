// Upstream bridge fake that exercises the send_upstream_data and send_response_trailers
// ABI callbacks (untested by the other fakes). encode_headers writes upstream data and
// half-closes the upstream connection; on_upstream_data emits response trailers.
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

  // Write some bytes upstream and end-stream the connection. This walks the buffer.add +
  // enableHalfClose + connection.write paths in HttpTcpBridge::sendUpstreamData.
  const char* payload = "hello-upstream";
  envoy_dynamic_module_type_module_buffer payload_buf = {.ptr = payload, .length = 14};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_upstream_data(bridge_envoy_ptr,
                                                                            payload_buf, true);

  // Also exercise the empty-data, end_stream=true branch (buffer empty, half-close still fires).
  envoy_dynamic_module_type_module_buffer empty_buf = {.ptr = NULL, .length = 0};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_upstream_data(bridge_envoy_ptr,
                                                                            empty_buf, true);
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
  (void)bridge_module_ptr;
  (void)end_of_stream;

  // Send a response with headers, then trailers — this hits sendResponseHeaders and the
  // sendResponseTrailers paths in HttpTcpBridge.
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_headers(bridge_envoy_ptr,
                                                                               200, NULL, 0, false);

  envoy_dynamic_module_type_module_http_header trailers[2];
  const char* k0 = "x-trailer";
  const char* v0 = "first";
  trailers[0].key_ptr = (char*)k0;
  trailers[0].key_length = 9;
  trailers[0].value_ptr = (char*)v0;
  trailers[0].value_length = 5;
  const char* k1 = "x-status";
  const char* v1 = "ok";
  trailers[1].key_ptr = (char*)k1;
  trailers[1].key_length = 8;
  trailers[1].value_ptr = (char*)v1;
  trailers[1].value_length = 2;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_trailers(bridge_envoy_ptr,
                                                                                trailers, 2);

  // Also exercise send_response_trailers with NULL trailers vector (early-return loop guard).
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_trailers(bridge_envoy_ptr,
                                                                                NULL, 0);
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr) {
  (void)bridge_module_ptr;
}
