#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

#include "envoy/network/connection.h"

#include "source/common/common/empty_string.h"

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

Network::PostIoAction
toPostIoAction(envoy_dynamic_module_type_transport_socket_post_io_action action) {
  return action == envoy_dynamic_module_type_transport_socket_post_io_action_Close
             ? Network::PostIoAction::Close
             : Network::PostIoAction::KeepOpen;
}

} // namespace

DynamicModuleTransportSocketConfig::DynamicModuleTransportSocketConfig(
    bool implements_secure_transport,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : implements_secure_transport_(implements_secure_transport),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleTransportSocketConfig::~DynamicModuleTransportSocketConfig() {
  if (in_module_config_ != nullptr && on_factory_config_destroy_ != nullptr) {
    on_factory_config_destroy_(in_module_config_);
    in_module_config_ = nullptr;
  }
}

absl::StatusOr<DynamicModuleTransportSocketConfigSharedPtr> newDynamicModuleTransportSocketConfig(
    const std::string& socket_name, const std::string& socket_config, bool is_upstream,
    bool implements_secure_transport,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module) {

  auto on_config_new = dynamic_module->getFunctionPointer<OnTransportSocketFactoryConfigNewType>(
      "envoy_dynamic_module_on_transport_socket_factory_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy =
      dynamic_module->getFunctionPointer<OnTransportSocketFactoryConfigDestroyType>(
          "envoy_dynamic_module_on_transport_socket_factory_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_new = dynamic_module->getFunctionPointer<OnTransportSocketNewType>(
      "envoy_dynamic_module_on_transport_socket_new");
  RETURN_IF_NOT_OK_REF(on_new.status());

  auto on_destroy = dynamic_module->getFunctionPointer<OnTransportSocketDestroyType>(
      "envoy_dynamic_module_on_transport_socket_destroy");
  RETURN_IF_NOT_OK_REF(on_destroy.status());

  auto on_set_callbacks = dynamic_module->getFunctionPointer<OnTransportSocketSetCallbacksType>(
      "envoy_dynamic_module_on_transport_socket_set_callbacks");
  RETURN_IF_NOT_OK_REF(on_set_callbacks.status());

  auto on_connected = dynamic_module->getFunctionPointer<OnTransportSocketOnConnectedType>(
      "envoy_dynamic_module_on_transport_socket_on_connected");
  RETURN_IF_NOT_OK_REF(on_connected.status());

  auto on_do_read = dynamic_module->getFunctionPointer<OnTransportSocketDoReadType>(
      "envoy_dynamic_module_on_transport_socket_do_read");
  RETURN_IF_NOT_OK_REF(on_do_read.status());

  auto on_do_write = dynamic_module->getFunctionPointer<OnTransportSocketDoWriteType>(
      "envoy_dynamic_module_on_transport_socket_do_write");
  RETURN_IF_NOT_OK_REF(on_do_write.status());

  auto on_close = dynamic_module->getFunctionPointer<OnTransportSocketCloseType>(
      "envoy_dynamic_module_on_transport_socket_close");
  RETURN_IF_NOT_OK_REF(on_close.status());

  auto on_get_protocol = dynamic_module->getFunctionPointer<OnTransportSocketGetProtocolType>(
      "envoy_dynamic_module_on_transport_socket_get_protocol");
  RETURN_IF_NOT_OK_REF(on_get_protocol.status());

  auto on_get_failure_reason =
      dynamic_module->getFunctionPointer<OnTransportSocketGetFailureReasonType>(
          "envoy_dynamic_module_on_transport_socket_get_failure_reason");
  RETURN_IF_NOT_OK_REF(on_get_failure_reason.status());

  auto on_can_flush_close = dynamic_module->getFunctionPointer<OnTransportSocketCanFlushCloseType>(
      "envoy_dynamic_module_on_transport_socket_can_flush_close");
  RETURN_IF_NOT_OK_REF(on_can_flush_close.status());

  auto on_start_secure_transport =
      dynamic_module->getFunctionPointer<OnTransportSocketStartSecureTransportType>(
          "envoy_dynamic_module_on_transport_socket_start_secure_transport");
  RETURN_IF_NOT_OK_REF(on_start_secure_transport.status());

  auto config = std::make_shared<DynamicModuleTransportSocketConfig>(implements_secure_transport,
                                                                     std::move(dynamic_module));

  config->on_factory_config_destroy_ = on_config_destroy.value();
  config->on_new_ = on_new.value();
  config->on_destroy_ = on_destroy.value();
  config->on_set_callbacks_ = on_set_callbacks.value();
  config->on_connected_ = on_connected.value();
  config->on_do_read_ = on_do_read.value();
  config->on_do_write_ = on_do_write.value();
  config->on_close_ = on_close.value();
  config->on_get_protocol_ = on_get_protocol.value();
  config->on_get_failure_reason_ = on_get_failure_reason.value();
  config->on_can_flush_close_ = on_can_flush_close.value();
  config->on_start_secure_transport_ = on_start_secure_transport.value();

  envoy_dynamic_module_type_envoy_buffer name_buffer = {socket_name.data(), socket_name.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {socket_config.data(),
                                                          socket_config.size()};
  config->in_module_config_ = on_config_new.value()(static_cast<void*>(config.get()), name_buffer,
                                                    config_buffer, is_upstream);
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to initialize dynamic module transport socket config");
  }
  return config;
}

DynamicModuleTransportSocket::DynamicModuleTransportSocket(
    DynamicModuleTransportSocketConfigSharedPtr config)
    : config_(std::move(config)) {
  in_module_socket_ = config_->on_new_(config_->in_module_config_, this);
  if (in_module_socket_ == nullptr) {
    ENVOY_LOG(error, "dynamic module failed to create transport socket; connection will be closed");
  }
}

DynamicModuleTransportSocket::~DynamicModuleTransportSocket() {
  if (in_module_socket_ != nullptr) {
    config_->on_destroy_(in_module_socket_);
    in_module_socket_ = nullptr;
  }
}

void DynamicModuleTransportSocket::setTransportSocketCallbacks(
    Network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
  if (in_module_socket_ != nullptr) {
    config_->on_set_callbacks_(this, in_module_socket_);
  }
}

std::string DynamicModuleTransportSocket::protocol() const {
  if (in_module_socket_ == nullptr) {
    return EMPTY_STRING;
  }
  envoy_dynamic_module_type_module_buffer result = {nullptr, 0};
  config_->on_get_protocol_(const_cast<DynamicModuleTransportSocket*>(this), in_module_socket_,
                            &result);
  if (result.ptr == nullptr || result.length == 0) {
    return EMPTY_STRING;
  }
  return std::string(result.ptr, result.length);
}

absl::string_view DynamicModuleTransportSocket::failureReason() const {
  if (in_module_socket_ == nullptr) {
    return EMPTY_STRING;
  }
  envoy_dynamic_module_type_module_buffer result = {nullptr, 0};
  config_->on_get_failure_reason_(const_cast<DynamicModuleTransportSocket*>(this),
                                  in_module_socket_, &result);
  if (result.ptr == nullptr || result.length == 0) {
    failure_reason_.clear();
    return EMPTY_STRING;
  }
  failure_reason_.assign(result.ptr, result.length);
  return failure_reason_;
}

bool DynamicModuleTransportSocket::canFlushClose() {
  if (in_module_socket_ == nullptr) {
    return true;
  }
  return config_->on_can_flush_close_(this, in_module_socket_);
}

void DynamicModuleTransportSocket::closeSocket(Network::ConnectionEvent event, bool abort_reset) {
  if (in_module_socket_ != nullptr) {
    config_->on_close_(this, in_module_socket_, toAbiConnectionEvent(event), abort_reset);
  }
}

Network::IoResult DynamicModuleTransportSocket::doRead(Buffer::Instance& buffer) {
  if (in_module_socket_ == nullptr) {
    return {Network::PostIoAction::Close, 0, false};
  }
  current_read_buffer_ = &buffer;
  auto result = config_->on_do_read_(this, in_module_socket_);
  current_read_buffer_ = nullptr;
  return {toPostIoAction(result.action), result.bytes_processed, result.end_stream_read};
}

Network::IoResult DynamicModuleTransportSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (in_module_socket_ == nullptr) {
    return {Network::PostIoAction::Close, 0, false};
  }
  current_write_buffer_ = &buffer;
  auto result = config_->on_do_write_(this, in_module_socket_, end_stream);
  current_write_buffer_ = nullptr;
  // end_stream_read is only meaningful for reads. The connection layer asserts it is never set on a
  // write result, so it is forced to false here regardless of what the module reports.
  return {toPostIoAction(result.action), result.bytes_processed, false};
}

void DynamicModuleTransportSocket::onConnected() {
  if (in_module_socket_ != nullptr) {
    config_->on_connected_(this, in_module_socket_);
  }
}

bool DynamicModuleTransportSocket::startSecureTransport() {
  if (in_module_socket_ == nullptr) {
    return false;
  }
  return config_->on_start_secure_transport_(this, in_module_socket_);
}

Network::TransportSocketPtr
DynamicModuleDownstreamTransportSocketFactory::createDownstreamTransportSocket() const {
  return std::make_unique<DynamicModuleTransportSocket>(config_);
}

Network::TransportSocketPtr DynamicModuleUpstreamTransportSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr, Upstream::HostDescriptionConstSharedPtr) const {
  return std::make_unique<DynamicModuleTransportSocket>(config_);
}

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
