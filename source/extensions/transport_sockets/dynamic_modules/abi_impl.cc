// NOLINT(namespace-envoy)

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/transport_socket_abi.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

namespace {

using namespace Envoy::Extensions::DynamicModules::TransportSockets;

} // namespace

// -----------------------------------------------------------------------------
// -------------------------------- Callbacks ----------------------------------
// -----------------------------------------------------------------------------

extern "C" {

int envoy_dynamic_module_callback_transport_socket_get_io_handle_fd(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr) {

  auto* socket = static_cast<DynamicModuleTransportSocket*>(socket_envoy_ptr);
  if (socket == nullptr || socket->callbacks() == nullptr) {
    return -1;
  }

  return socket->callbacks()->ioHandle().fdDoNotUse();
}

int envoy_dynamic_module_callback_transport_socket_io_handle_read(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr, void* buffer_ptr,
    size_t buffer_capacity, uint64_t* bytes_read) {

  auto* socket = static_cast<DynamicModuleTransportSocket*>(socket_envoy_ptr);
  if (socket == nullptr || socket->callbacks() == nullptr || buffer_ptr == nullptr ||
      bytes_read == nullptr) {
    return -1;
  }

  Envoy::Buffer::OwnedImpl temp_buffer;
  auto result = socket->callbacks()->ioHandle().read(temp_buffer, buffer_capacity);

  if (!result.ok()) {
    if (result.err_->getErrorCode() == Envoy::Api::IoError::IoErrorCode::Again) {
      return -2; // EAGAIN/EWOULDBLOCK.
    }
    return -1;
  }

  *bytes_read = result.return_value_;

  // Copy data from buffer to the provided buffer.
  if (*bytes_read > 0) {
    temp_buffer.copyOut(0, *bytes_read, buffer_ptr);
  }

  return 0;
}

int envoy_dynamic_module_callback_transport_socket_io_handle_write(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr, const void* buffer_ptr,
    size_t buffer_length, uint64_t* bytes_written) {

  auto* socket = static_cast<DynamicModuleTransportSocket*>(socket_envoy_ptr);
  if (socket == nullptr || socket->callbacks() == nullptr || buffer_ptr == nullptr ||
      bytes_written == nullptr) {
    return -1;
  }

  Envoy::Buffer::OwnedImpl temp_buffer;
  temp_buffer.add(buffer_ptr, buffer_length);

  auto result = socket->callbacks()->ioHandle().write(temp_buffer);

  if (!result.ok()) {
    if (result.err_->getErrorCode() == Envoy::Api::IoError::IoErrorCode::Again) {
      return -2; // EAGAIN/EWOULDBLOCK.
    }
    return -1;
  }

  *bytes_written = result.return_value_;
  return 0;
}

bool envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr) {

  auto* socket = static_cast<DynamicModuleTransportSocket*>(socket_envoy_ptr);
  if (socket == nullptr || socket->callbacks() == nullptr) {
    return false;
  }

  return socket->callbacks()->shouldDrainReadBuffer();
}

void envoy_dynamic_module_callback_transport_socket_set_readable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr) {

  auto* socket = static_cast<DynamicModuleTransportSocket*>(socket_envoy_ptr);
  if (socket == nullptr || socket->callbacks() == nullptr) {
    return;
  }

  socket->callbacks()->setTransportSocketIsReadable();
}

void envoy_dynamic_module_callback_transport_socket_raise_event(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr,
    envoy_dynamic_module_type_connection_event event) {

  auto* socket = static_cast<DynamicModuleTransportSocket*>(socket_envoy_ptr);
  if (socket == nullptr || socket->callbacks() == nullptr) {
    return;
  }

  Envoy::Network::ConnectionEvent envoy_event;
  switch (event) {
  case envoy_dynamic_module_type_connection_event_RemoteClose:
    envoy_event = Envoy::Network::ConnectionEvent::RemoteClose;
    break;
  case envoy_dynamic_module_type_connection_event_LocalClose:
    envoy_event = Envoy::Network::ConnectionEvent::LocalClose;
    break;
  case envoy_dynamic_module_type_connection_event_Connected:
    envoy_event = Envoy::Network::ConnectionEvent::Connected;
    break;
  case envoy_dynamic_module_type_connection_event_ConnectedZeroRtt:
    envoy_event = Envoy::Network::ConnectionEvent::ConnectedZeroRtt;
    break;
  default:
    return;
  }

  socket->callbacks()->raiseEvent(envoy_event);
}

void envoy_dynamic_module_callback_transport_socket_flush_write_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr) {

  auto* socket = static_cast<DynamicModuleTransportSocket*>(socket_envoy_ptr);
  if (socket == nullptr || socket->callbacks() == nullptr) {
    return;
  }

  socket->callbacks()->flushWriteBuffer();
}

void envoy_dynamic_module_callback_transport_socket_log(int level, const char* message_ptr,
                                                        size_t message_length) {
  if (message_ptr == nullptr) {
    return;
  }

  std::string message(message_ptr, message_length);

  switch (level) {
  case 0: // trace.
    ENVOY_LOG_MISC(trace, "[dynamic_module_transport_socket] {}", message);
    break;
  case 1: // debug.
    ENVOY_LOG_MISC(debug, "[dynamic_module_transport_socket] {}", message);
    break;
  case 2: // info.
    ENVOY_LOG_MISC(info, "[dynamic_module_transport_socket] {}", message);
    break;
  case 3: // warn.
    ENVOY_LOG_MISC(warn, "[dynamic_module_transport_socket] {}", message);
    break;
  case 4: // error.
    ENVOY_LOG_MISC(error, "[dynamic_module_transport_socket] {}", message);
    break;
  case 5: // critical.
    ENVOY_LOG_MISC(critical, "[dynamic_module_transport_socket] {}", message);
    break;
  default:
    ENVOY_LOG_MISC(info, "[dynamic_module_transport_socket] {}", message);
    break;
  }
}

} // extern "C"
