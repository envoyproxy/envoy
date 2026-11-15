#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// A module that only creates sockets and leaves their I/O to the kernel. Its configuration
// selects how it answers, so tests can drive Envoy's handling of each answer:
// - empty: an IPv4 stream socket with TCP_NODELAY set, the custom step the standard interface
//   would not take, and UseDefault for every other socket.
// - "default": UseDefault for every socket.
// - "error": Error for every socket.
// - "flag_without_hooks": an IPv4 socket with the DelegateIo flag, which needs I/O hooks this
//   module does not implement.
// - "wrong_family": an IPv6 socket for an IPv4 request.

static int config_dummy = 0;

typedef enum {
  mode_create,
  mode_default,
  mode_error,
  mode_flag_without_hooks,
  mode_wrong_family,
} creation_mode;

static creation_mode mode = mode_create;
static int state_dummy = 0;
static int close_count = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

static int config_is(envoy_dynamic_module_type_envoy_buffer config, const char* value) {
  return config.length == strlen(value) && memcmp(config.ptr, value, config.length) == 0;
}

envoy_dynamic_module_type_socket_interface_config_module_ptr
envoy_dynamic_module_on_socket_interface_config_new(
    envoy_dynamic_module_type_socket_interface_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  if (config_is(config, "default")) {
    mode = mode_default;
  } else if (config_is(config, "error")) {
    mode = mode_error;
  } else if (config_is(config, "flag_without_hooks")) {
    mode = mode_flag_without_hooks;
  } else if (config_is(config, "wrong_family")) {
    mode = mode_wrong_family;
  } else {
    mode = mode_create;
  }
  return &config_dummy;
}

void envoy_dynamic_module_on_socket_interface_config_destroy(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

bool envoy_dynamic_module_on_socket_interface_ip_family_supported(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr, int domain) {
  (void)config_module_ptr;
  return domain == AF_INET;
}

envoy_dynamic_module_type_socket_interface_creation_result
envoy_dynamic_module_on_socket_interface_socket(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_type socket_type,
    envoy_dynamic_module_type_envoy_buffer address, uint32_t port, int domain, bool socket_v6only,
    const envoy_dynamic_module_type_socket_interface_creation_options* options, int64_t* fd_out,
    uint32_t* flags_out,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr* socket_module_ptr_out) {
  (void)config_module_ptr;
  (void)address;
  (void)port;
  (void)socket_v6only;
  (void)options;
  if (mode == mode_default) {
    return envoy_dynamic_module_type_socket_interface_creation_result_UseDefault;
  }
  if (mode == mode_error) {
    return envoy_dynamic_module_type_socket_interface_creation_result_Error;
  }
  if (domain != AF_INET ||
      socket_type != envoy_dynamic_module_type_socket_interface_socket_type_Stream) {
    return envoy_dynamic_module_type_socket_interface_creation_result_UseDefault;
  }
  int fd = socket(mode == mode_wrong_family ? AF_INET6 : AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    return envoy_dynamic_module_type_socket_interface_creation_result_Error;
  }
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  *fd_out = fd;
  if (mode == mode_flag_without_hooks) {
    *flags_out = envoy_dynamic_module_type_socket_interface_socket_flag_DelegateIo;
    *socket_module_ptr_out = &state_dummy;
  }
  return envoy_dynamic_module_type_socket_interface_creation_result_Created;
}

void envoy_dynamic_module_on_socket_interface_socket_close(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr) {
  (void)config_module_ptr;
  (void)socket_module_ptr;
  close_count++;
}

// Test helpers below are not part of the socket interface ABI.

int getSocketInterfaceCreationOnlyCloseCount(void) { return close_count; }
