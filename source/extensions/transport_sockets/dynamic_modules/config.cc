#include "source/extensions/transport_sockets/dynamic_modules/config.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

DynamicModuleTransportSocketFactoryConfig::DynamicModuleTransportSocketFactoryConfig(
    const std::string& socket_name, const std::string& socket_config, bool is_upstream,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : socket_name_(socket_name), socket_config_(socket_config), is_upstream_(is_upstream),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleTransportSocketFactoryConfig::~DynamicModuleTransportSocketFactoryConfig() {
  if (in_module_config_ != nullptr && on_factory_config_destroy_ != nullptr) {
    on_factory_config_destroy_(in_module_config_);
    in_module_config_ = nullptr;
  }
}

absl::StatusOr<DynamicModuleTransportSocketFactoryConfigSharedPtr>
newDynamicModuleTransportSocketFactoryConfig(
    const std::string& socket_name, const std::string& socket_config, bool is_upstream,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module) {

  // Resolve the symbols for the transport socket using graceful error handling.
  auto on_config_new = dynamic_module->getFunctionPointer<OnTransportSocketFactoryConfigNewType>(
      "envoy_dynamic_module_on_transport_socket_factory_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy =
      dynamic_module->getFunctionPointer<OnTransportSocketFactoryConfigDestroyType>(
          "envoy_dynamic_module_on_transport_socket_factory_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_socket_new = dynamic_module->getFunctionPointer<OnTransportSocketNewType>(
      "envoy_dynamic_module_on_transport_socket_new");
  RETURN_IF_NOT_OK_REF(on_socket_new.status());

  auto on_socket_destroy = dynamic_module->getFunctionPointer<OnTransportSocketDestroyType>(
      "envoy_dynamic_module_on_transport_socket_destroy");
  RETURN_IF_NOT_OK_REF(on_socket_destroy.status());

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

  auto config = std::make_shared<DynamicModuleTransportSocketFactoryConfig>(
      socket_name, socket_config, is_upstream, std::move(dynamic_module));

  // Store the resolved function pointers.
  config->on_factory_config_destroy_ = on_config_destroy.value();
  config->on_transport_socket_new_ = on_socket_new.value();
  config->on_transport_socket_destroy_ = on_socket_destroy.value();
  config->on_transport_socket_set_callbacks_ = on_set_callbacks.value();
  config->on_transport_socket_on_connected_ = on_connected.value();
  config->on_transport_socket_do_read_ = on_do_read.value();
  config->on_transport_socket_do_write_ = on_do_write.value();
  config->on_transport_socket_close_ = on_close.value();
  config->on_transport_socket_get_protocol_ = on_get_protocol.value();
  config->on_transport_socket_get_failure_reason_ = on_get_failure_reason.value();
  config->on_transport_socket_can_flush_close_ = on_can_flush_close.value();

  // Create the in-module configuration.
  envoy_dynamic_module_type_envoy_buffer name_buffer = {socket_name.data(), socket_name.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {socket_config.data(),
                                                          socket_config.size()};
  config->in_module_config_ = on_config_new.value()(static_cast<void*>(config.get()), name_buffer,
                                                    config_buffer, is_upstream);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to initialize dynamic module transport socket factory config");
  }
  return config;
}

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
