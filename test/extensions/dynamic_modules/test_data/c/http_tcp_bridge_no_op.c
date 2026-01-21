#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

static int some_variable = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return kAbiVersion;
}

envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr
envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new(
    envoy_dynamic_module_type_envoy_buffer name,
    envoy_dynamic_module_type_envoy_buffer config) {
  (void)name;
  (void)config;
  return &some_variable;
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_config_destroy(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr config_ptr) {
  (void)config_ptr;
}

envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr
envoy_dynamic_module_on_upstream_http_tcp_bridge_new(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr config_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr) {
  (void)config_ptr;
  (void)bridge_envoy_ptr;
  return &some_variable + 1;
}

envoy_dynamic_module_type_upstream_http_tcp_bridge_status
envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_envoy_ptr;
  (void)bridge_module_ptr;
  (void)end_of_stream;
  return envoy_dynamic_module_type_upstream_http_tcp_bridge_status_Continue;
}

envoy_dynamic_module_type_upstream_http_tcp_bridge_status
envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_envoy_ptr;
  (void)bridge_module_ptr;
  (void)end_of_stream;
  return envoy_dynamic_module_type_upstream_http_tcp_bridge_status_Continue;
}

envoy_dynamic_module_type_upstream_http_tcp_bridge_status
envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream) {
  (void)bridge_envoy_ptr;
  (void)bridge_module_ptr;
  (void)end_of_stream;
  return envoy_dynamic_module_type_upstream_http_tcp_bridge_status_Continue;
}

void envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr) {
  (void)bridge_module_ptr;
}

