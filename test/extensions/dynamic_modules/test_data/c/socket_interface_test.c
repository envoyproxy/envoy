#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// A module that performs the I/O of the sockets it creates. A client socket is one end of a
// connected IPv4 TCP pair whose other end, the peer, stays with the module for the tests to drive,
// handed to Envoy as already connected. In listen mode the module instead creates a listen socket
// that hands over connections, each another connected pair. A socket can serve a greeting from the
// module's memory before reading its file descriptor, which the kernel never signals, so its
// readiness comes only from the module.
//
// The switches and counters are module-global because the module stays loaded across tests
// (do_not_close); every test resets the switches it uses.

static int config_dummy = 0;

typedef struct {
  int fd;
  int domain;
  // The module's end of the connected pair, or -1 for a listen socket or a copy.
  int peer_fd;
  envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr ready_handle;
  char greeting[64];
  size_t greeting_length;
  size_t greeting_offset;
} test_socket_state;

static test_socket_state* last_state = NULL;
static test_socket_state* last_listen_state = NULL;
static int close_count = 0;
static int accept_count = 0;
static int duplicate_count = 0;
static int pending_accepts = 0;

static int force_bad_fd = 0;
static int fail_io = 0;
static int listen_mode = 0;
static int report_accept_addresses = 0;
// 1 reports a write of 0 bytes, 2 reports 5 bytes more than offered on reads and writes, and 3
// makes writes return Again.
static int byte_report = 0;
// Hands back a client socket with 1 both flags, 2 HandsOverConnections or 3 no per-socket state.
static int create_flags_mode = 0;
// Hands over a connection with 1 no flags, 2 an invalid flag, 3 DelegateIo but no per-socket
// state, 4 an unconnected socket, 5 an invalid port or 6 an IPv6 address.
static int accept_mode = 0;
static char greeting[64];
static size_t greeting_length = 0;

static char last_address[108]; // sizeof(((struct sockaddr_un*)0)->sun_path)
static uint32_t last_port = 0;
static int last_domain = 0;
static int last_v6only = 0;

static const char accept_local_address[] = "10.1.2.3";
static const char accept_peer_address[] = "10.9.8.7";
static const char accept_local_pipe[] = "/tmp/socket_interface_test_local";
static const char accept_peer_pipe[] = "/tmp/socket_interface_test_peer";

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

static void set_non_blocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static test_socket_state* new_state(int fd, int peer_fd) {
  test_socket_state* state = calloc(1, sizeof(test_socket_state));
  if (state == NULL) {
    return NULL;
  }
  state->fd = fd;
  state->domain = AF_INET;
  state->peer_fd = peer_fd;
  memcpy(state->greeting, greeting, greeting_length);
  state->greeting_length = greeting_length;
  return state;
}

// Creates a connected IPv4 TCP pair: the client side goes to Envoy, the peer stays in the state.
static test_socket_state* create_connected_pair(void) {
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    return NULL;
  }
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  socklen_t addr_len = sizeof(addr);
  if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(listener, 1) < 0 ||
      getsockname(listener, (struct sockaddr*)&addr, &addr_len) < 0) {
    close(listener);
    return NULL;
  }
  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) {
    close(listener);
    return NULL;
  }
  if (connect(client_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(listener);
    close(client_fd);
    return NULL;
  }
  int peer_fd = accept(listener, NULL, NULL);
  close(listener);
  if (peer_fd < 0) {
    close(client_fd);
    return NULL;
  }
  set_non_blocking(client_fd);
  set_non_blocking(peer_fd);
  test_socket_state* state = new_state(client_fd, peer_fd);
  if (state == NULL) {
    close(client_fd);
    close(peer_fd);
  }
  return state;
}

// Creates a connected AF_UNIX stream pair, which is unnamed.
static test_socket_state* create_unix_pair(void) {
  int fds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
    return NULL;
  }
  set_non_blocking(fds[0]);
  set_non_blocking(fds[1]);
  test_socket_state* state = new_state(fds[0], fds[1]);
  if (state == NULL) {
    close(fds[0]);
    close(fds[1]);
    return NULL;
  }
  state->domain = AF_UNIX;
  return state;
}

// Drops a state Envoy never receives, leaving its file descriptor to Envoy.
static void release_state(test_socket_state* state) {
  close(state->peer_fd);
  free(state);
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
    uint32_t* flags_out,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr* socket_module_ptr_out) {
  (void)config_module_ptr;
  (void)options;
  memset(last_address, 0, sizeof(last_address));
  if (address.length > 0) {
    size_t copy =
        address.length < sizeof(last_address) - 1 ? address.length : sizeof(last_address) - 1;
    memcpy(last_address, address.ptr, copy);
  }
  last_port = port;
  last_domain = domain;
  last_v6only = socket_v6only ? 1 : 0;

  if (force_bad_fd) {
    // A per-socket state with an invalid file descriptor, which Envoy rejects and releases.
    *fd_out = -1;
    *flags_out = envoy_dynamic_module_type_socket_interface_socket_flag_DelegateIo;
    *socket_module_ptr_out = new_state(-1, -1);
    return envoy_dynamic_module_type_socket_interface_creation_result_Created;
  }
  const bool stream = socket_type == envoy_dynamic_module_type_socket_interface_socket_type_Stream;
  if (listen_mode && stream && (domain == AF_INET || domain == AF_UNIX)) {
    int fd = socket(domain, SOCK_STREAM, 0);
    test_socket_state* state = fd < 0 ? NULL : new_state(fd, -1);
    if (state == NULL) {
      if (fd >= 0) {
        close(fd);
      }
      return envoy_dynamic_module_type_socket_interface_creation_result_Error;
    }
    set_non_blocking(fd);
    state->domain = domain;
    last_listen_state = state;
    *fd_out = fd;
    *flags_out = envoy_dynamic_module_type_socket_interface_socket_flag_HandsOverConnections;
    *socket_module_ptr_out = state;
    return envoy_dynamic_module_type_socket_interface_creation_result_Created;
  }
  if (domain != AF_INET || !stream) {
    return envoy_dynamic_module_type_socket_interface_creation_result_UseDefault;
  }
  test_socket_state* state = create_connected_pair();
  if (state == NULL) {
    return envoy_dynamic_module_type_socket_interface_creation_result_Error;
  }
  *fd_out = state->fd;
  switch (create_flags_mode) {
  case 1:
    *flags_out = envoy_dynamic_module_type_socket_interface_socket_flag_DelegateIo |
                 envoy_dynamic_module_type_socket_interface_socket_flag_HandsOverConnections;
    *socket_module_ptr_out = state;
    return envoy_dynamic_module_type_socket_interface_creation_result_Created;
  case 2:
    *flags_out = envoy_dynamic_module_type_socket_interface_socket_flag_HandsOverConnections;
    *socket_module_ptr_out = state;
    return envoy_dynamic_module_type_socket_interface_creation_result_AlreadyConnected;
  case 3:
    release_state(state);
    *flags_out = envoy_dynamic_module_type_socket_interface_socket_flag_DelegateIo;
    return envoy_dynamic_module_type_socket_interface_creation_result_AlreadyConnected;
  default:
    break;
  }
  last_state = state;
  *flags_out = envoy_dynamic_module_type_socket_interface_socket_flag_DelegateIo;
  *socket_module_ptr_out = state;
  return envoy_dynamic_module_type_socket_interface_creation_result_AlreadyConnected;
}

void envoy_dynamic_module_on_socket_interface_socket_close(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr) {
  (void)config_module_ptr;
  test_socket_state* state = (test_socket_state*)socket_module_ptr;
  if (state->ready_handle != NULL) {
    envoy_dynamic_module_callback_socket_interface_io_ready_delete(state->ready_handle);
  }
  if (state->peer_fd >= 0) {
    close(state->peer_fd);
  }
  if (last_state == state) {
    last_state = NULL;
  }
  if (last_listen_state == state) {
    last_listen_state = NULL;
  }
  free(state);
  close_count++;
}

envoy_dynamic_module_type_socket_interface_io_status
envoy_dynamic_module_on_socket_interface_socket_duplicate(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr, int64_t* fd_out,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr* duplicate_socket_module_ptr_out) {
  (void)config_module_ptr;
  if (fail_io) {
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  const test_socket_state* state = (const test_socket_state*)socket_module_ptr;
  // With force_bad_fd, the copy is a socket of another family, which Envoy rejects.
  int fd = force_bad_fd ? socket(AF_UNIX, SOCK_STREAM, 0) : dup(state->fd);
  test_socket_state* copy = fd < 0 ? NULL : new_state(fd, -1);
  if (copy == NULL) {
    if (fd >= 0) {
      close(fd);
    }
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  copy->domain = state->domain;
  if (state == last_listen_state) {
    last_listen_state = copy;
  }
  duplicate_count++;
  *fd_out = fd;
  *duplicate_socket_module_ptr_out = copy;
  return envoy_dynamic_module_type_socket_interface_io_status_Success;
}

void envoy_dynamic_module_on_socket_interface_set_socket_handle(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr,
    envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr
        io_ready_handle_module_ptr) {
  (void)config_module_ptr;
  test_socket_state* state = (test_socket_state*)socket_module_ptr;
  state->ready_handle = io_ready_handle_module_ptr;
  if (state->greeting_length > 0) {
    // The kernel never signals the greeting, and Envoy has not registered the socket yet, so this
    // readiness must survive until it does.
    envoy_dynamic_module_callback_socket_interface_io_ready(
        io_ready_handle_module_ptr, envoy_dynamic_module_type_socket_interface_io_event_Read);
  }
}

envoy_dynamic_module_type_socket_interface_io_status
envoy_dynamic_module_on_socket_interface_io_read(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr,
    envoy_dynamic_module_type_socket_interface_io_slice* slices, size_t num_slices, bool peek,
    uint64_t* bytes_transferred) {
  (void)config_module_ptr;
  if (fail_io) {
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  test_socket_state* state = (test_socket_state*)socket_module_ptr;
  if (state->greeting_offset < state->greeting_length) {
    // Serve the greeting from memory, filling the slices as one prefix.
    size_t remaining = state->greeting_length - state->greeting_offset;
    size_t copied = 0;
    for (size_t i = 0; i < num_slices && copied < remaining; i++) {
      size_t chunk = slices[i].length < remaining - copied ? slices[i].length : remaining - copied;
      memcpy(slices[i].ptr, state->greeting + state->greeting_offset + copied, chunk);
      copied += chunk;
      if (chunk < slices[i].length) {
        break;
      }
    }
    if (!peek) {
      state->greeting_offset += copied;
    }
    *bytes_transferred = copied;
    return envoy_dynamic_module_type_socket_interface_io_status_Success;
  }
  ssize_t n;
  if (peek) {
    n = num_slices == 0 ? 0 : recv(state->fd, slices[0].ptr, slices[0].length, MSG_PEEK);
  } else {
    struct iovec iov[16];
    size_t count = num_slices < 16 ? num_slices : 16;
    for (size_t i = 0; i < count; i++) {
      iov[i].iov_base = slices[i].ptr;
      iov[i].iov_len = slices[i].length;
    }
    n = readv(state->fd, iov, (int)count);
  }
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return envoy_dynamic_module_type_socket_interface_io_status_Again;
    }
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  *bytes_transferred = (uint64_t)n + (byte_report == 2 ? 5 : 0);
  return envoy_dynamic_module_type_socket_interface_io_status_Success;
}

envoy_dynamic_module_type_socket_interface_io_status
envoy_dynamic_module_on_socket_interface_io_write(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr,
    const envoy_dynamic_module_type_envoy_buffer* buffers, size_t num_buffers,
    uint64_t* bytes_transferred) {
  (void)config_module_ptr;
  if (fail_io) {
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  test_socket_state* state = (test_socket_state*)socket_module_ptr;
  struct iovec iov[16];
  size_t count = num_buffers < 16 ? num_buffers : 16;
  for (size_t i = 0; i < count; i++) {
    iov[i].iov_base = (void*)buffers[i].ptr;
    iov[i].iov_len = buffers[i].length;
  }
  if (byte_report == 1) {
    *bytes_transferred = 0;
    return envoy_dynamic_module_type_socket_interface_io_status_Success;
  }
  if (byte_report == 3) {
    return envoy_dynamic_module_type_socket_interface_io_status_Again;
  }
  ssize_t n = writev(state->fd, iov, (int)count);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return envoy_dynamic_module_type_socket_interface_io_status_Again;
    }
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  *bytes_transferred = (uint64_t)n + (byte_report == 2 ? 5 : 0);
  return envoy_dynamic_module_type_socket_interface_io_status_Success;
}

// Built without this hook as socket_interface_missing_io_shutdown, whose sockets Envoy shuts down
// with shutdown(2).
#ifndef SOCKET_INTERFACE_TEST_WITHOUT_IO_SHUTDOWN
envoy_dynamic_module_type_socket_interface_io_status
envoy_dynamic_module_on_socket_interface_io_shutdown(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr, int how) {
  (void)config_module_ptr;
  if (fail_io) {
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  test_socket_state* state = (test_socket_state*)socket_module_ptr;
  if (shutdown(state->fd, how) < 0) {
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  return envoy_dynamic_module_type_socket_interface_io_status_Success;
}
#endif

envoy_dynamic_module_type_socket_interface_io_status
envoy_dynamic_module_on_socket_interface_accept(
    envoy_dynamic_module_type_socket_interface_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr, int64_t* fd_out,
    uint32_t* flags_out,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr* accepted_socket_module_ptr_out,
    envoy_dynamic_module_type_socket_interface_address* local_address_out,
    envoy_dynamic_module_type_socket_interface_address* peer_address_out) {
  (void)config_module_ptr;
  if (fail_io) {
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  if (pending_accepts == 0) {
    return envoy_dynamic_module_type_socket_interface_io_status_Again;
  }
  pending_accepts--;
  if (force_bad_fd) {
    *fd_out = -1;
    return envoy_dynamic_module_type_socket_interface_io_status_Success;
  }
  if (accept_mode == 4) {
    *fd_out = socket(AF_INET, SOCK_STREAM, 0);
    return envoy_dynamic_module_type_socket_interface_io_status_Success;
  }
  const test_socket_state* listen_state = (const test_socket_state*)socket_module_ptr;
  const bool unix_domain = listen_state->domain == AF_UNIX;
  test_socket_state* state = unix_domain ? create_unix_pair() : create_connected_pair();
  if (state == NULL) {
    return envoy_dynamic_module_type_socket_interface_io_status_Error;
  }
  accept_count++;
  *fd_out = state->fd;
  *flags_out = envoy_dynamic_module_type_socket_interface_socket_flag_DelegateIo;
  *accepted_socket_module_ptr_out = state;
  if (accept_mode == 3) {
    release_state(state);
    *accepted_socket_module_ptr_out = NULL;
    return envoy_dynamic_module_type_socket_interface_io_status_Success;
  }
  last_state = state;
  if (accept_mode == 1) {
    *flags_out = 0;
  } else if (accept_mode == 2) {
    *flags_out = envoy_dynamic_module_type_socket_interface_socket_flag_HandsOverConnections;
  }
  if (report_accept_addresses || accept_mode == 5 || accept_mode == 6) {
    const char* local = unix_domain ? accept_local_pipe : accept_local_address;
    const char* peer = unix_domain ? accept_peer_pipe : accept_peer_address;
    local_address_out->address.ptr = local;
    local_address_out->address.length = strlen(local);
    local_address_out->port = unix_domain ? 0 : (accept_mode == 5 ? 70000 : 443);
    if (accept_mode == 6) {
      peer = "::1";
    }
    peer_address_out->address.ptr = peer;
    peer_address_out->address.length = strlen(peer);
    peer_address_out->port = unix_domain ? 0 : 1234;
  }
  return envoy_dynamic_module_type_socket_interface_io_status_Success;
}

// Test helpers below are not part of the socket interface ABI.

void setSocketInterfaceTestForceBadFd(int enabled) { force_bad_fd = enabled; }

void setSocketInterfaceTestFailIo(int enabled) { fail_io = enabled; }

void setSocketInterfaceTestByteReport(int mode) { byte_report = mode; }

void setSocketInterfaceTestListenMode(int enabled) { listen_mode = enabled; }

void setSocketInterfaceTestReportAcceptAddresses(int enabled) { report_accept_addresses = enabled; }

void setSocketInterfaceTestCreateFlagsMode(int mode) { create_flags_mode = mode; }

void setSocketInterfaceTestAcceptMode(int mode) { accept_mode = mode; }

void setSocketInterfaceTestGreeting(const char* value) {
  greeting_length = value == NULL ? 0 : strlen(value);
  if (greeting_length > sizeof(greeting)) {
    greeting_length = sizeof(greeting);
  }
  memcpy(greeting, value == NULL ? "" : value, greeting_length);
}

// Queues connections on the last listen socket and reports them from the calling thread.
void queueSocketInterfaceTestConnections(int count) {
  pending_accepts += count;
  if (last_listen_state != NULL && last_listen_state->ready_handle != NULL) {
    envoy_dynamic_module_callback_socket_interface_io_ready(
        last_listen_state->ready_handle, envoy_dynamic_module_type_socket_interface_io_event_Read);
  }
}

const char* getSocketInterfaceTestLastAddress(void) { return last_address; }

uint32_t getSocketInterfaceTestLastPort(void) { return last_port; }

int getSocketInterfaceTestLastDomain(void) { return last_domain; }

int getSocketInterfaceTestLastV6only(void) { return last_v6only; }

int getSocketInterfaceTestAcceptCount(void) { return accept_count; }

int getSocketInterfaceTestDuplicateCount(void) { return duplicate_count; }

int getSocketInterfaceTestCloseCount(void) { return close_count; }

int getSocketInterfaceTestPeerFd(void) { return last_state == NULL ? -1 : last_state->peer_fd; }

void triggerSocketInterfaceTestReady(uint32_t events) {
  if (last_state != NULL && last_state->ready_handle != NULL) {
    envoy_dynamic_module_callback_socket_interface_io_ready(last_state->ready_handle, events);
  }
}
