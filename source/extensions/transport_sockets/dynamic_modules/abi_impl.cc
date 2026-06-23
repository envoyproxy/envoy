#include <algorithm>

#include "envoy/api/io_error.h"
#include "envoy/common/platform.h"
#include "envoy/event/file_event.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"

#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

namespace {

DynamicModuleTransportSocket*
toSocket(envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  return static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
}

// Fills the address string and port from an IP address. Returns false for null or non-IP addresses.
bool fillIpAddress(const Network::Address::InstanceConstSharedPtr& address,
                   envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  if (address == nullptr || address->ip() == nullptr) {
    *port_out = 0;
    return false;
  }
  const std::string& addr_str = address->ip()->addressAsString();
  *address_out = {addr_str.data(), addr_str.size()};
  *port_out = address->ip()->port();
  return true;
}

} // namespace

// Transport socket callbacks. These are the strong definitions that override the weak stubs in
// source/extensions/dynamic_modules/abi_impl.cc when this extension is linked.
extern "C" {

envoy_dynamic_module_type_transport_socket_io_status
envoy_dynamic_module_callback_transport_socket_io_read(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr, char* buffer,
    size_t length, size_t* bytes_read) {
  *bytes_read = 0;
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return envoy_dynamic_module_type_transport_socket_io_status_Error;
  }
  Api::IoCallUint64Result result = callbacks->ioHandle().recv(buffer, length, 0);
  if (result.ok()) {
    *bytes_read = result.return_value_;
    return envoy_dynamic_module_type_transport_socket_io_status_Success;
  }
  if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
    return envoy_dynamic_module_type_transport_socket_io_status_Again;
  }
  return envoy_dynamic_module_type_transport_socket_io_status_Error;
}

envoy_dynamic_module_type_transport_socket_io_status
envoy_dynamic_module_callback_transport_socket_io_write(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    const char* buffer, size_t length, size_t* bytes_written) {
  *bytes_written = 0;
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return envoy_dynamic_module_type_transport_socket_io_status_Error;
  }
  Buffer::RawSlice slice = {const_cast<char*>(buffer), length};
  Api::IoCallUint64Result result = callbacks->ioHandle().writev(&slice, 1);
  if (result.ok()) {
    *bytes_written = result.return_value_;
    return envoy_dynamic_module_type_transport_socket_io_status_Success;
  }
  if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
    return envoy_dynamic_module_type_transport_socket_io_status_Again;
  }
  return envoy_dynamic_module_type_transport_socket_io_status_Error;
}

void envoy_dynamic_module_callback_transport_socket_io_shutdown_write(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return;
  }
  callbacks->ioHandle().shutdown(ENVOY_SHUT_WR);
}

int envoy_dynamic_module_callback_transport_socket_get_fd(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return -1;
  }
  return static_cast<int>(callbacks->ioHandle().fdDoNotUse());
}

void envoy_dynamic_module_callback_transport_socket_read_buffer_add(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    const char* data, size_t length) {
  Buffer::Instance* buffer = toSocket(transport_socket_envoy_ptr)->currentReadBuffer();
  if (buffer == nullptr || data == nullptr || length == 0) {
    return;
  }
  buffer->add(data, length);
}

void envoy_dynamic_module_callback_transport_socket_write_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    size_t length) {
  Buffer::Instance* buffer = toSocket(transport_socket_envoy_ptr)->currentWriteBuffer();
  if (buffer == nullptr) {
    return;
  }
  buffer->drain(std::min<uint64_t>(length, buffer->length()));
}

void envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* slices, size_t* slices_count) {
  Buffer::Instance* buffer = toSocket(transport_socket_envoy_ptr)->currentWriteBuffer();
  if (buffer == nullptr) {
    *slices_count = 0;
    return;
  }
  Buffer::RawSliceVector raw_slices = buffer->getRawSlices();
  if (slices == nullptr) {
    *slices_count = raw_slices.size();
    return;
  }
  const size_t count = std::min(*slices_count, raw_slices.size());
  for (size_t i = 0; i < count; i++) {
    slices[i].ptr = static_cast<const char*>(raw_slices[i].mem_);
    slices[i].length = raw_slices[i].len_;
  }
  *slices_count = count;
}

size_t envoy_dynamic_module_callback_transport_socket_write_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  Buffer::Instance* buffer = toSocket(transport_socket_envoy_ptr)->currentWriteBuffer();
  if (buffer == nullptr) {
    return 0;
  }
  return buffer->length();
}

void envoy_dynamic_module_callback_transport_socket_raise_event(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_network_connection_event event) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return;
  }
  switch (event) {
  case envoy_dynamic_module_type_network_connection_event_RemoteClose:
    callbacks->raiseEvent(Network::ConnectionEvent::RemoteClose);
    return;
  case envoy_dynamic_module_type_network_connection_event_LocalClose:
    callbacks->raiseEvent(Network::ConnectionEvent::LocalClose);
    return;
  case envoy_dynamic_module_type_network_connection_event_Connected:
    callbacks->raiseEvent(Network::ConnectionEvent::Connected);
    return;
  case envoy_dynamic_module_type_network_connection_event_ConnectedZeroRtt:
    callbacks->raiseEvent(Network::ConnectionEvent::ConnectedZeroRtt);
    return;
  }
}

bool envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return false;
  }
  return callbacks->shouldDrainReadBuffer();
}

void envoy_dynamic_module_callback_transport_socket_set_is_readable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return;
  }
  callbacks->setTransportSocketIsReadable();
}

void envoy_dynamic_module_callback_transport_socket_set_is_writable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return;
  }
  // TransportSocketCallbacks has no `writability` setter, so re-arm the write event directly.
  callbacks->ioHandle().activateFileEvents(Event::FileReadyType::Write);
}

void envoy_dynamic_module_callback_transport_socket_flush_write_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    return;
  }
  callbacks->flushWriteBuffer();
}

bool envoy_dynamic_module_callback_transport_socket_get_remote_address(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    *port_out = 0;
    return false;
  }
  return fillIpAddress(callbacks->connection().connectionInfoProvider().remoteAddress(),
                       address_out, port_out);
}

bool envoy_dynamic_module_callback_transport_socket_get_local_address(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  Network::TransportSocketCallbacks* callbacks = toSocket(transport_socket_envoy_ptr)->callbacks();
  if (callbacks == nullptr) {
    *port_out = 0;
    return false;
  }
  return fillIpAddress(callbacks->connection().connectionInfoProvider().localAddress(), address_out,
                       port_out);
}

} // extern "C"

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
