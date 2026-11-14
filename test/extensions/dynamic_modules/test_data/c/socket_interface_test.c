#include "source/extensions/dynamic_modules/abi/abi.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

static int config_dummy = 0;

// Per-socket state of one created socket: the Envoy-owned socket, the module-owned peer of the
// connected pair, and the readiness handle Envoy handed to the module.
typedef struct {
  int fd;
  int peer_fd;
  envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr ready_handle;
} test_socket_state;

static test_socket_state* last_state = NULL;
static int close_count = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

static void set_non_blocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
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
  return domain == AF_INET;
}

envoy_dynamic_module_type_socket_interface_creation_result
envoy_dynamic_module_on_socket_interface_socket(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_type socket_type,
    envoy_dynamic_module_type_envoy_buffer address, uint32_t port, int domain, bool socket_v6only,
    const envoy_dynamic_module_type_socket_interface_creation_options* options, int64_t* fd_out,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr* socket_module_ptr_out) {
  (void)config_module_ptr;
  (void)address;
  (void)port;
  (void)socket_v6only;
  (void)options;
  if (domain != AF_INET || socket_type != envoy_dynamic_module_type_socket_interface_socket_type_Stream) {
    return envoy_dynamic_module_type_socket_interface_creation_result_UNSUPPORTED_FAMILY;
  }

  // Create a connected IPv4 TCP pair: the client side is handed to Envoy, the peer stays with
  // the module for the tests to drive.
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    return envoy_dynamic_module_type_socket_interface_creation_result_ERROR;
  }
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
      listen(listener, 1) < 0) {
    close(listener);
    return envoy_dynamic_module_type_socket_interface_creation_result_ERROR;
  }
  socklen_t addr_len = sizeof(addr);
  getsockname(listener, (struct sockaddr*)&addr, &addr_len);

  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) {
    close(listener);
    return envoy_dynamic_module_type_socket_interface_creation_result_ERROR;
  }
  if (connect(client_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(listener);
    close(client_fd);
    return envoy_dynamic_module_type_socket_interface_creation_result_ERROR;
  }
  int peer_fd = accept(listener, NULL, NULL);
  close(listener);
  if (peer_fd < 0) {
    close(client_fd);
    return envoy_dynamic_module_type_socket_interface_creation_result_ERROR;
  }
  set_non_blocking(client_fd);
  set_non_blocking(peer_fd);

  test_socket_state* state = calloc(1, sizeof(test_socket_state));
  if (state == NULL) {
    close(client_fd);
    close(peer_fd);
    return envoy_dynamic_module_type_socket_interface_creation_result_ERROR;
  }
  state->fd = client_fd;
  state->peer_fd = peer_fd;
  last_state = state;

  *fd_out = client_fd;
  *socket_module_ptr_out = state;
  return envoy_dynamic_module_type_socket_interface_creation_result_ALREADY_CONNECTED;
}

void envoy_dynamic_module_on_socket_interface_socket_close(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr) {
  (void)config_module_ptr;
  test_socket_state* state = socket_module_ptr;
  if (state == NULL) {
    return;
  }
  if (state->ready_handle != NULL) {
    envoy_dynamic_module_callback_socket_interface_io_ready_delete(state->ready_handle);
    state->ready_handle = NULL;
  }
  close(state->peer_fd);
  free(state);
  close_count++;
}

void envoy_dynamic_module_on_socket_interface_set_socket_handle(
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr,
    envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr
        io_ready_handle_module_ptr) {
  test_socket_state* state = socket_module_ptr;
  state->ready_handle = io_ready_handle_module_ptr;
}

envoy_dynamic_module_type_socket_interface_io_status
envoy_dynamic_module_on_socket_interface_io_read(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr,
    envoy_dynamic_module_type_socket_interface_io_slice* slices, size_t num_slices,
    uint64_t* bytes_transferred) {
  (void)config_module_ptr;
  test_socket_state* state = socket_module_ptr;
  struct iovec iov[num_slices];
  for (size_t i = 0; i < num_slices; i++) {
    iov[i].iov_base = slices[i].ptr;
    iov[i].iov_len = slices[i].length;
  }
  ssize_t n = readv(state->fd, iov, (int)num_slices);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return envoy_dynamic_module_type_socket_interface_io_status_Again;
    }
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  *bytes_transferred = (uint64_t)n;
  return envoy_dynamic_module_type_socket_interface_io_status_Success;
}

envoy_dynamic_module_type_socket_interface_io_status
envoy_dynamic_module_on_socket_interface_io_write(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr,
    const envoy_dynamic_module_type_envoy_buffer* buffers, size_t num_buffers,
    uint64_t* bytes_transferred) {
  (void)config_module_ptr;
  test_socket_state* state = socket_module_ptr;
  struct iovec iov[num_buffers];
  for (size_t i = 0; i < num_buffers; i++) {
    iov[i].iov_base = (void*)buffers[i].ptr;
    iov[i].iov_len = buffers[i].length;
  }
  ssize_t n = writev(state->fd, iov, (int)num_buffers);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return envoy_dynamic_module_type_socket_interface_io_status_Again;
    }
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  *bytes_transferred = (uint64_t)n;
  return envoy_dynamic_module_type_socket_interface_io_status_Success;
}

envoy_dynamic_module_type_socket_interface_io_status
envoy_dynamic_module_on_socket_interface_io_shutdown(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr, int how) {
  (void)config_module_ptr;
  test_socket_state* state = socket_module_ptr;
  if (shutdown(state->fd, how) < 0) {
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  return envoy_dynamic_module_type_socket_interface_io_status_Success;
}

// Test helpers below are not part of the socket interface ABI.

int getSocketInterfaceTestCloseCount(void) { return close_count; }

int getSocketInterfaceTestPeerFd(void) { return last_state == NULL ? -1 : last_state->peer_fd; }

void triggerSocketInterfaceTestReady(uint32_t events) {
  if (last_state != NULL && last_state->ready_handle != NULL) {
    envoy_dynamic_module_callback_socket_interface_io_ready(last_state->ready_handle, events);
  }
}
