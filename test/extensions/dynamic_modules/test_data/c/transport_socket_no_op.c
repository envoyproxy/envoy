#include <assert.h>

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/transport_socket_abi.h"

static int some_variable = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_transport_socket_factory_config_module_ptr
envoy_dynamic_module_on_transport_socket_factory_config_new(
    envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr factory_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer socket_name,
    envoy_dynamic_module_type_envoy_buffer socket_config, bool is_upstream) {
  (void)factory_config_envoy_ptr;
  (void)socket_name;
  (void)socket_config;
  (void)is_upstream;
  return &some_variable;
}

void envoy_dynamic_module_on_transport_socket_factory_config_destroy(
    envoy_dynamic_module_type_transport_socket_factory_config_module_ptr factory_config_ptr) {
  assert(factory_config_ptr == &some_variable);
}

envoy_dynamic_module_type_transport_socket_module_ptr envoy_dynamic_module_on_transport_socket_new(
    envoy_dynamic_module_type_transport_socket_factory_config_module_ptr factory_config_ptr,
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  (void)factory_config_ptr;
  (void)transport_socket_envoy_ptr;
  return &some_variable + 1;
}

void envoy_dynamic_module_on_transport_socket_destroy(
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr) {
  assert(transport_socket_module_ptr == &some_variable + 1);
}

void envoy_dynamic_module_on_transport_socket_set_callbacks(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
}

void envoy_dynamic_module_on_transport_socket_on_connected(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
}

envoy_dynamic_module_type_transport_socket_io_result
envoy_dynamic_module_on_transport_socket_do_read(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
  envoy_dynamic_module_type_transport_socket_io_result result = {
      envoy_dynamic_module_type_transport_socket_post_io_action_KeepOpen, 0, false};
  return result;
}

envoy_dynamic_module_type_transport_socket_io_result
envoy_dynamic_module_on_transport_socket_do_write(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    size_t write_buffer_length, bool end_stream) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
  (void)write_buffer_length;
  (void)end_stream;
  envoy_dynamic_module_type_transport_socket_io_result result = {
      envoy_dynamic_module_type_transport_socket_post_io_action_KeepOpen, 0, false};
  return result;
}

void envoy_dynamic_module_on_transport_socket_close(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    envoy_dynamic_module_type_network_connection_event event) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
  (void)event;
}

void envoy_dynamic_module_on_transport_socket_get_protocol(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    envoy_dynamic_module_type_module_buffer* result) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
  result->ptr = NULL;
  result->length = 0;
}

void envoy_dynamic_module_on_transport_socket_get_failure_reason(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    envoy_dynamic_module_type_module_buffer* result) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
  result->ptr = NULL;
  result->length = 0;
}

bool envoy_dynamic_module_on_transport_socket_can_flush_close(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
  return true;
}

