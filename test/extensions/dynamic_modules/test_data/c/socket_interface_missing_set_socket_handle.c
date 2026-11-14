#include "source/extensions/dynamic_modules/abi/abi.h"

static int config_dummy = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_socket_interface_config_module_ptr
envoy_dynamic_module_on_socket_interface_config_new(
    envoy_dynamic_module_type_socket_interface_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  return &config_dummy;
}

void envoy_dynamic_module_on_socket_interface_config_destroy(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

bool envoy_dynamic_module_on_socket_interface_ip_family_supported(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr, int domain) {
  (void)config_module_ptr;
  (void)domain;
  return true;
}

envoy_dynamic_module_type_socket_interface_creation_result
envoy_dynamic_module_on_socket_interface_socket(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_type socket_type,
    envoy_dynamic_module_type_envoy_buffer address, uint32_t port, int domain, bool socket_v6only,
    const envoy_dynamic_module_type_socket_interface_creation_options* options, int64_t* fd_out,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr* socket_module_ptr_out) {
  (void)config_module_ptr;
  (void)socket_type;
  (void)address;
  (void)port;
  (void)domain;
  (void)socket_v6only;
  (void)options;
  (void)fd_out;
  (void)socket_module_ptr_out;
  return envoy_dynamic_module_type_socket_interface_creation_result_ERROR;
}

void envoy_dynamic_module_on_socket_interface_socket_close(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr) {
  (void)config_module_ptr;
  (void)socket_module_ptr;
}

envoy_dynamic_module_type_socket_interface_io_status
envoy_dynamic_module_on_socket_interface_io_write(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr,
    const envoy_dynamic_module_type_envoy_buffer* buffers, size_t num_buffers,
    uint64_t* bytes_transferred) {
  (void)config_module_ptr;
  (void)socket_module_ptr;
  (void)buffers;
  (void)num_buffers;
  *bytes_transferred = 0;
  return envoy_dynamic_module_type_socket_interface_io_status_Success;
}

envoy_dynamic_module_type_socket_interface_io_status
envoy_dynamic_module_on_socket_interface_io_shutdown(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr, int how) {
  (void)config_module_ptr;
  (void)socket_module_ptr;
  (void)how;
  return envoy_dynamic_module_type_socket_interface_io_status_Success;
}
