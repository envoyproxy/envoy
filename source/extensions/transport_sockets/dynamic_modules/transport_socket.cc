#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

#include "envoy/network/connection.h"

#include "source/extensions/transport_sockets/dynamic_modules/config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

namespace {

envoy_dynamic_module_type_network_connection_event
toAbiConnectionEvent(Network::ConnectionEvent event) {
  switch (event) {
  case Network::ConnectionEvent::RemoteClose:
    return envoy_dynamic_module_type_network_connection_event_RemoteClose;
  case Network::ConnectionEvent::LocalClose:
    return envoy_dynamic_module_type_network_connection_event_LocalClose;
  case Network::ConnectionEvent::Connected:
    return envoy_dynamic_module_type_network_connection_event_Connected;
  case Network::ConnectionEvent::ConnectedZeroRtt:
    return envoy_dynamic_module_type_network_connection_event_ConnectedZeroRtt;
  }
  return envoy_dynamic_module_type_network_connection_event_LocalClose;
}

Network::IoResult toEnvoyIoResult(envoy_dynamic_module_type_transport_socket_io_result result) {
  Network::PostIoAction action = Network::PostIoAction::KeepOpen;
  if (result.action == envoy_dynamic_module_type_transport_socket_post_io_action_Close) {
    action = Network::PostIoAction::Close;
  }
  return Network::IoResult{action, result.bytes_processed, result.end_stream_read};
}

} // namespace

DynamicModuleTransportSocket::DynamicModuleTransportSocket(
    DynamicModuleTransportSocketFactoryConfigSharedPtr factory_config)
    : factory_config_(std::move(factory_config)) {
  in_module_socket_ = factory_config_->on_transport_socket_new_(factory_config_->in_module_config_,
                                                                thisAsVoidPtr());
  if (in_module_socket_ == nullptr) {
    ENVOY_LOG(error, "dynamic modules: failed to create in-module transport socket");
  }
}

DynamicModuleTransportSocket::~DynamicModuleTransportSocket() { destroy(); }

void DynamicModuleTransportSocket::destroy() {
  if (destroyed_) {
    return;
  }
  if (in_module_socket_ != nullptr) {
    factory_config_->on_transport_socket_destroy_(in_module_socket_);
    in_module_socket_ = nullptr;
  }
  destroyed_ = true;
}

void DynamicModuleTransportSocket::setTransportSocketCallbacks(
    Network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
  if (in_module_socket_ != nullptr) {
    factory_config_->on_transport_socket_set_callbacks_(thisAsVoidPtr(), in_module_socket_);
  }
}

std::string DynamicModuleTransportSocket::protocol() const {
  if (in_module_socket_ == nullptr) {
    return "";
  }
  envoy_dynamic_module_type_module_buffer result = {nullptr, 0};
  factory_config_->on_transport_socket_get_protocol_(
      const_cast<DynamicModuleTransportSocket*>(this)->thisAsVoidPtr(), in_module_socket_, &result);
  if (result.ptr != nullptr && result.length > 0) {
    cached_protocol_.assign(result.ptr, result.length);
    return cached_protocol_;
  }
  return "";
}

absl::string_view DynamicModuleTransportSocket::failureReason() const {
  if (in_module_socket_ == nullptr) {
    return "";
  }
  envoy_dynamic_module_type_module_buffer result = {nullptr, 0};
  factory_config_->on_transport_socket_get_failure_reason_(
      const_cast<DynamicModuleTransportSocket*>(this)->thisAsVoidPtr(), in_module_socket_, &result);
  if (result.ptr != nullptr && result.length > 0) {
    cached_failure_reason_.assign(result.ptr, result.length);
    return cached_failure_reason_;
  }
  return "";
}

bool DynamicModuleTransportSocket::canFlushClose() {
  if (in_module_socket_ == nullptr) {
    return true;
  }
  return factory_config_->on_transport_socket_can_flush_close_(thisAsVoidPtr(), in_module_socket_);
}

void DynamicModuleTransportSocket::closeSocket(Network::ConnectionEvent event) {
  if (in_module_socket_ == nullptr) {
    return;
  }
  factory_config_->on_transport_socket_close_(thisAsVoidPtr(), in_module_socket_,
                                              toAbiConnectionEvent(event));
}

Network::IoResult DynamicModuleTransportSocket::doRead(Buffer::Instance& buffer) {
  if (in_module_socket_ == nullptr) {
    return {Network::PostIoAction::Close, 0, false};
  }
  current_read_buffer_ = &buffer;
  auto result = factory_config_->on_transport_socket_do_read_(thisAsVoidPtr(), in_module_socket_);
  current_read_buffer_ = nullptr;
  return toEnvoyIoResult(result);
}

Network::IoResult DynamicModuleTransportSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (in_module_socket_ == nullptr) {
    return {Network::PostIoAction::Close, 0, false};
  }
  current_write_buffer_ = &buffer;
  auto result = factory_config_->on_transport_socket_do_write_(thisAsVoidPtr(), in_module_socket_,
                                                               buffer.length(), end_stream);
  current_write_buffer_ = nullptr;
  return toEnvoyIoResult(result);
}

void DynamicModuleTransportSocket::onConnected() {
  if (in_module_socket_ == nullptr) {
    return;
  }
  factory_config_->on_transport_socket_on_connected_(thisAsVoidPtr(), in_module_socket_);
}

Ssl::ConnectionInfoConstSharedPtr DynamicModuleTransportSocket::ssl() const {
  // Dynamic module transport sockets do not expose SSL connection info through this interface.
  return nullptr;
}

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
