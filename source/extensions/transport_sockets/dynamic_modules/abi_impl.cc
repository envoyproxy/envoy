#include "envoy/network/connection.h"

#include "source/extensions/dynamic_modules/transport_socket_abi.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

namespace {

DynamicModuleTransportSocket* getTransportSocket(void* ptr) {
  return static_cast<DynamicModuleTransportSocket*>(ptr);
}

} // namespace

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

using namespace Envoy::Extensions::TransportSockets::DynamicModules;

extern "C" {

void* envoy_dynamic_module_callback_transport_socket_get_io_handle(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = getTransportSocket(transport_socket_envoy_ptr);
  if (socket == nullptr || socket->callbacks() == nullptr) {
    return nullptr;
  }
  return &socket->callbacks()->ioHandle();
}

int64_t envoy_dynamic_module_callback_transport_socket_io_handle_read(void* io_handle, char* buffer,
                                                                      size_t length,
                                                                      size_t* bytes_read) {
  if (io_handle == nullptr || buffer == nullptr || bytes_read == nullptr) {
    return -1;
  }
  auto* handle = static_cast<Envoy::Network::IoHandle*>(io_handle);
  auto result = handle->recv(buffer, length, 0);
  if (!result.ok()) {
    *bytes_read = 0;
    return static_cast<int64_t>(result.err_->getErrorCode());
  }
  *bytes_read = result.return_value_;
  return 0;
}

int64_t envoy_dynamic_module_callback_transport_socket_io_handle_write(void* io_handle,
                                                                       const char* buffer,
                                                                       size_t length,
                                                                       size_t* bytes_written) {
  if (io_handle == nullptr || buffer == nullptr || bytes_written == nullptr) {
    return -1;
  }
  auto* handle = static_cast<Envoy::Network::IoHandle*>(io_handle);
  Envoy::Buffer::RawSlice slice{const_cast<char*>(buffer), length};
  auto result = handle->writev(&slice, 1);
  if (!result.ok()) {
    *bytes_written = 0;
    return static_cast<int64_t>(result.err_->getErrorCode());
  }
  *bytes_written = result.return_value_;
  return 0;
}

void envoy_dynamic_module_callback_transport_socket_read_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    size_t length) {
  auto* socket = getTransportSocket(transport_socket_envoy_ptr);
  if (socket == nullptr || socket->currentReadBuffer() == nullptr) {
    return;
  }
  socket->currentReadBuffer()->drain(length);
}

void envoy_dynamic_module_callback_transport_socket_read_buffer_add(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    const char* data, size_t length) {
  auto* socket = getTransportSocket(transport_socket_envoy_ptr);
  if (socket == nullptr || socket->currentReadBuffer() == nullptr) {
    return;
  }
  socket->currentReadBuffer()->add(data, length);
}

size_t envoy_dynamic_module_callback_transport_socket_read_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = getTransportSocket(transport_socket_envoy_ptr);
  if (socket == nullptr || socket->currentReadBuffer() == nullptr) {
    return 0;
  }
  return socket->currentReadBuffer()->length();
}

void envoy_dynamic_module_callback_transport_socket_write_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    size_t length) {
  auto* socket = getTransportSocket(transport_socket_envoy_ptr);
  if (socket == nullptr || socket->currentWriteBuffer() == nullptr) {
    return;
  }
  socket->currentWriteBuffer()->drain(length);
}

void envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* slices, size_t* slices_count) {
  auto* socket = getTransportSocket(transport_socket_envoy_ptr);
  if (socket == nullptr || socket->currentWriteBuffer() == nullptr || slices == nullptr ||
      slices_count == nullptr) {
    if (slices_count != nullptr) {
      *slices_count = 0;
    }
    return;
  }

  const size_t max_slices = *slices_count;
  auto raw_slices = socket->currentWriteBuffer()->getRawSlices();
  size_t count = std::min(max_slices, raw_slices.size());

  for (size_t i = 0; i < count; ++i) {
    slices[i].ptr = static_cast<const char*>(raw_slices[i].mem_);
    slices[i].length = raw_slices[i].len_;
  }
  *slices_count = count;
}

size_t envoy_dynamic_module_callback_transport_socket_write_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = getTransportSocket(transport_socket_envoy_ptr);
  if (socket == nullptr || socket->currentWriteBuffer() == nullptr) {
    return 0;
  }
  return socket->currentWriteBuffer()->length();
}

void envoy_dynamic_module_callback_transport_socket_raise_event(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_network_connection_event event) {
  auto* socket = getTransportSocket(transport_socket_envoy_ptr);
  if (socket == nullptr || socket->callbacks() == nullptr) {
    return;
  }
  Envoy::Network::ConnectionEvent envoy_event;
  switch (event) {
  case envoy_dynamic_module_type_network_connection_event_RemoteClose:
    envoy_event = Envoy::Network::ConnectionEvent::RemoteClose;
    break;
  case envoy_dynamic_module_type_network_connection_event_LocalClose:
    envoy_event = Envoy::Network::ConnectionEvent::LocalClose;
    break;
  case envoy_dynamic_module_type_network_connection_event_Connected:
    envoy_event = Envoy::Network::ConnectionEvent::Connected;
    break;
  case envoy_dynamic_module_type_network_connection_event_ConnectedZeroRtt:
    envoy_event = Envoy::Network::ConnectionEvent::ConnectedZeroRtt;
    break;
  default:
    envoy_event = Envoy::Network::ConnectionEvent::LocalClose;
    break;
  }
  socket->callbacks()->raiseEvent(envoy_event);
}

bool envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = getTransportSocket(transport_socket_envoy_ptr);
  if (socket == nullptr || socket->callbacks() == nullptr) {
    return false;
  }
  return socket->callbacks()->shouldDrainReadBuffer();
}

void envoy_dynamic_module_callback_transport_socket_set_is_readable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = getTransportSocket(transport_socket_envoy_ptr);
  if (socket == nullptr || socket->callbacks() == nullptr) {
    return;
  }
  socket->callbacks()->setTransportSocketIsReadable();
}

void envoy_dynamic_module_callback_transport_socket_flush_write_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = getTransportSocket(transport_socket_envoy_ptr);
  if (socket == nullptr || socket->callbacks() == nullptr) {
    return;
  }
  socket->callbacks()->flushWriteBuffer();
}

} // extern "C"
