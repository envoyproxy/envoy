#include "envoy/network/connection.h"

#include "source/extensions/dynamic_modules/transport_socket_abi.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

namespace {

Network::ConnectionEvent
toEnvoyConnectionEvent(envoy_dynamic_module_type_network_connection_event event) {
  switch (event) {
  case envoy_dynamic_module_type_network_connection_event_RemoteClose:
    return Network::ConnectionEvent::RemoteClose;
  case envoy_dynamic_module_type_network_connection_event_LocalClose:
    return Network::ConnectionEvent::LocalClose;
  case envoy_dynamic_module_type_network_connection_event_Connected:
    return Network::ConnectionEvent::Connected;
  case envoy_dynamic_module_type_network_connection_event_ConnectedZeroRtt:
    return Network::ConnectionEvent::ConnectedZeroRtt;
  }
  return Network::ConnectionEvent::LocalClose;
}

} // namespace

extern "C" {

void* envoy_dynamic_module_callback_transport_socket_get_io_handle(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
  auto* callbacks = socket->callbacks();
  if (callbacks == nullptr) {
    return nullptr;
  }
  return &callbacks->ioHandle();
}

int64_t envoy_dynamic_module_callback_transport_socket_io_handle_read(void* io_handle, char* buffer,
                                                                      size_t length,
                                                                      size_t* bytes_read) {
  if (io_handle == nullptr || buffer == nullptr || bytes_read == nullptr) {
    return -1;
  }
  auto* handle = static_cast<Network::IoHandle*>(io_handle);
  auto result = handle->recv(buffer, length, 0);
  if (result.err_ != nullptr) {
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
  auto* handle = static_cast<Network::IoHandle*>(io_handle);
  Buffer::RawSlice slice{const_cast<char*>(buffer), length};
  auto result = handle->writev(&slice, 1);
  if (result.err_ != nullptr) {
    *bytes_written = 0;
    return static_cast<int64_t>(result.err_->getErrorCode());
  }
  *bytes_written = result.return_value_;
  return 0;
}

void envoy_dynamic_module_callback_transport_socket_read_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    size_t length) {
  auto* socket = static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
  Buffer::Instance* buffer = socket->currentReadBuffer();
  if (buffer != nullptr) {
    buffer->drain(length);
  }
}

void envoy_dynamic_module_callback_transport_socket_read_buffer_add(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    const char* data, size_t length) {
  auto* socket = static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
  Buffer::Instance* buffer = socket->currentReadBuffer();
  if (buffer != nullptr) {
    buffer->add(data, length);
  }
}

size_t envoy_dynamic_module_callback_transport_socket_read_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
  Buffer::Instance* buffer = socket->currentReadBuffer();
  if (buffer == nullptr) {
    return 0;
  }
  return buffer->length();
}

void envoy_dynamic_module_callback_transport_socket_write_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    size_t length) {
  auto* socket = static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
  Buffer::Instance* buffer = socket->currentWriteBuffer();
  if (buffer != nullptr) {
    buffer->drain(length);
  }
}

void envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* slices, size_t* slices_count) {
  auto* socket = static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
  Buffer::Instance* buffer = socket->currentWriteBuffer();
  if (buffer == nullptr || slices_count == nullptr) {
    if (slices_count != nullptr) {
      *slices_count = 0;
    }
    return;
  }
  Buffer::RawSliceVector raw_slices = buffer->getRawSlices();
  // When slices is null, return the number of slices without copying (query mode).
  if (slices == nullptr) {
    *slices_count = raw_slices.size();
    return;
  }
  size_t count = std::min(raw_slices.size(), *slices_count);
  for (size_t i = 0; i < count; i++) {
    slices[i].ptr = static_cast<const char*>(raw_slices[i].mem_);
    slices[i].length = raw_slices[i].len_;
  }
  *slices_count = count;
}

size_t envoy_dynamic_module_callback_transport_socket_write_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
  Buffer::Instance* buffer = socket->currentWriteBuffer();
  if (buffer == nullptr) {
    return 0;
  }
  return buffer->length();
}

void envoy_dynamic_module_callback_transport_socket_raise_event(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_network_connection_event event) {
  auto* socket = static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
  auto* callbacks = socket->callbacks();
  if (callbacks != nullptr) {
    callbacks->raiseEvent(toEnvoyConnectionEvent(event));
  }
}

bool envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
  auto* callbacks = socket->callbacks();
  if (callbacks == nullptr) {
    return false;
  }
  return callbacks->shouldDrainReadBuffer();
}

void envoy_dynamic_module_callback_transport_socket_set_is_readable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
  auto* callbacks = socket->callbacks();
  if (callbacks != nullptr) {
    callbacks->setTransportSocketIsReadable();
  }
}

void envoy_dynamic_module_callback_transport_socket_flush_write_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = static_cast<DynamicModuleTransportSocket*>(transport_socket_envoy_ptr);
  auto* callbacks = socket->callbacks();
  if (callbacks != nullptr) {
    callbacks->flushWriteBuffer();
  }
}

} // extern "C"

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
