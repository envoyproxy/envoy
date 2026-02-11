#pragma once

#include <memory>
#include <string>

#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/transport_socket_abi.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

// Function pointer types for transport socket ABI functions.
using OnTransportSocketFactoryConfigNewType =
    decltype(&envoy_dynamic_module_on_transport_socket_factory_config_new);
using OnTransportSocketFactoryConfigDestroyType =
    decltype(&envoy_dynamic_module_on_transport_socket_factory_config_destroy);
using OnTransportSocketNewType = decltype(&envoy_dynamic_module_on_transport_socket_new);
using OnTransportSocketDestroyType = decltype(&envoy_dynamic_module_on_transport_socket_destroy);
using OnTransportSocketSetCallbacksType =
    decltype(&envoy_dynamic_module_on_transport_socket_set_callbacks);
using OnTransportSocketOnConnectedType =
    decltype(&envoy_dynamic_module_on_transport_socket_on_connected);
using OnTransportSocketDoReadType = decltype(&envoy_dynamic_module_on_transport_socket_do_read);
using OnTransportSocketDoWriteType = decltype(&envoy_dynamic_module_on_transport_socket_do_write);
using OnTransportSocketCloseType = decltype(&envoy_dynamic_module_on_transport_socket_close);
using OnTransportSocketGetProtocolType =
    decltype(&envoy_dynamic_module_on_transport_socket_get_protocol);
using OnTransportSocketGetFailureReasonType =
    decltype(&envoy_dynamic_module_on_transport_socket_get_failure_reason);
using OnTransportSocketCanFlushCloseType =
    decltype(&envoy_dynamic_module_on_transport_socket_can_flush_close);

/**
 * Configuration for transport socket factory based on a dynamic module.
 * This will be owned by the factory and used to create transport sockets.
 *
 * Note: Symbol resolution and in-module config creation are done in the
 * newDynamicModuleTransportSocketFactoryConfig() factory function to provide graceful error
 * handling.
 */
class DynamicModuleTransportSocketFactoryConfig {
public:
  DynamicModuleTransportSocketFactoryConfig(
      const std::string& socket_name, const std::string& socket_config, bool is_upstream,
      Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleTransportSocketFactoryConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_transport_socket_factory_config_module_ptr in_module_config_ = nullptr;

  // The function pointers for the module related to the transport socket. All of them are resolved
  // during newDynamicModuleTransportSocketFactoryConfig() and made sure they are not nullptr after
  // that.
  OnTransportSocketFactoryConfigDestroyType on_factory_config_destroy_ = nullptr;
  OnTransportSocketNewType on_transport_socket_new_ = nullptr;
  OnTransportSocketDestroyType on_transport_socket_destroy_ = nullptr;
  OnTransportSocketSetCallbacksType on_transport_socket_set_callbacks_ = nullptr;
  OnTransportSocketOnConnectedType on_transport_socket_on_connected_ = nullptr;
  OnTransportSocketDoReadType on_transport_socket_do_read_ = nullptr;
  OnTransportSocketDoWriteType on_transport_socket_do_write_ = nullptr;
  OnTransportSocketCloseType on_transport_socket_close_ = nullptr;
  OnTransportSocketGetProtocolType on_transport_socket_get_protocol_ = nullptr;
  OnTransportSocketGetFailureReasonType on_transport_socket_get_failure_reason_ = nullptr;
  OnTransportSocketCanFlushCloseType on_transport_socket_can_flush_close_ = nullptr;

  bool is_upstream() const { return is_upstream_; }
  const std::string& socket_name() const { return socket_name_; }

private:
  // Allow the factory function to access private members for initialization.
  friend absl::StatusOr<std::shared_ptr<DynamicModuleTransportSocketFactoryConfig>>
  newDynamicModuleTransportSocketFactoryConfig(
      const std::string& socket_name, const std::string& socket_config, bool is_upstream,
      Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  const std::string socket_name_;
  const std::string socket_config_;
  const bool is_upstream_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleTransportSocketFactoryConfigSharedPtr =
    std::shared_ptr<DynamicModuleTransportSocketFactoryConfig>;

/**
 * Creates a new DynamicModuleTransportSocketFactoryConfig for given configuration.
 * @param socket_name the name of the transport socket implementation in the module.
 * @param socket_config the configuration for the transport socket.
 * @param is_upstream true if this is for upstream connections, false for downstream.
 * @param dynamic_module the dynamic module to use.
 * @return a shared pointer to the new config object or an error if the module could not be loaded.
 */
absl::StatusOr<DynamicModuleTransportSocketFactoryConfigSharedPtr>
newDynamicModuleTransportSocketFactoryConfig(
    const std::string& socket_name, const std::string& socket_config, bool is_upstream,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
