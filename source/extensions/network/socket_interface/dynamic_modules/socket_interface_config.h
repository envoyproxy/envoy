#pragma once

#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

// Type aliases for function pointers resolved from the module.
using OnSocketInterfaceConfigNewType =
    decltype(&envoy_dynamic_module_on_socket_interface_config_new);
using OnSocketInterfaceConfigDestroyType =
    decltype(&envoy_dynamic_module_on_socket_interface_config_destroy);
using OnSocketInterfaceIpFamilySupportedType =
    decltype(&envoy_dynamic_module_on_socket_interface_ip_family_supported);
using OnSocketInterfaceSocketType = decltype(&envoy_dynamic_module_on_socket_interface_socket);
using OnSocketInterfaceSocketCloseType =
    decltype(&envoy_dynamic_module_on_socket_interface_socket_close);
using OnSocketInterfaceSetSocketHandleType =
    decltype(&envoy_dynamic_module_on_socket_interface_set_socket_handle);
using OnSocketInterfaceIoReadType = decltype(&envoy_dynamic_module_on_socket_interface_io_read);
using OnSocketInterfaceIoWriteType = decltype(&envoy_dynamic_module_on_socket_interface_io_write);
using OnSocketInterfaceIoShutdownType =
    decltype(&envoy_dynamic_module_on_socket_interface_io_shutdown);
using OnSocketInterfaceOnServerInitializedType =
    decltype(&envoy_dynamic_module_on_socket_interface_on_server_initialized);
using OnSocketInterfaceOnWorkerThreadInitializedType =
    decltype(&envoy_dynamic_module_on_socket_interface_on_worker_thread_initialized);

/**
 * Configuration for a dynamic module socket interface. This resolves and holds the symbols
 * used for socket creation and I/O delegation.
 *
 * Note: Symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleSocketInterfaceConfig() to provide graceful error handling. The constructor
 * only initializes basic members.
 */
class DynamicModuleSocketInterfaceConfig {
public:
  DynamicModuleSocketInterfaceConfig(absl::string_view socket_interface_name,
                                     absl::string_view socket_interface_config,
                                     Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleSocketInterfaceConfig();

  // Whether the module implements the I/O hooks. When it does, sockets are wrapped in the
  // delegating I/O handle; otherwise they use the standard fd-based handle.
  bool implementsIoHooks() const { return on_io_read_ != nullptr; }

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_socket_interface_config_module_ptr in_module_config_{nullptr};

  // Function pointers for the module. The required ones are resolved during
  // newDynamicModuleSocketInterfaceConfig() and guaranteed non-nullptr after that. The I/O
  // hooks are either all resolved or all left null.
  OnSocketInterfaceConfigDestroyType on_config_destroy_{nullptr};
  OnSocketInterfaceIpFamilySupportedType on_ip_family_supported_{nullptr};
  OnSocketInterfaceSocketType on_socket_{nullptr};
  OnSocketInterfaceSocketCloseType on_socket_close_{nullptr};
  OnSocketInterfaceSetSocketHandleType on_set_socket_handle_{nullptr};
  OnSocketInterfaceIoReadType on_io_read_{nullptr};
  OnSocketInterfaceIoWriteType on_io_write_{nullptr};
  OnSocketInterfaceIoShutdownType on_io_shutdown_{nullptr};
  OnSocketInterfaceOnServerInitializedType on_server_initialized_{nullptr};
  OnSocketInterfaceOnWorkerThreadInitializedType on_worker_thread_initialized_{nullptr};

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleSocketInterfaceConfig>>
  newDynamicModuleSocketInterfaceConfig(
      absl::string_view socket_interface_name, absl::string_view socket_interface_config,
      Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  const std::string socket_interface_name_;
  const std::string socket_interface_config_;
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleSocketInterfaceConfigSharedPtr =
    std::shared_ptr<DynamicModuleSocketInterfaceConfig>;

/**
 * Creates a new DynamicModuleSocketInterfaceConfig for the given configuration.
 * @param socket_interface_name the name identifying the implementation within the module.
 * @param socket_interface_config the configuration bytes for the implementation.
 * @param dynamic_module the dynamic module to use.
 * @return a shared pointer to the new config object or an error if symbol resolution failed.
 */
absl::StatusOr<DynamicModuleSocketInterfaceConfigSharedPtr>
newDynamicModuleSocketInterfaceConfig(absl::string_view socket_interface_name,
                                      absl::string_view socket_interface_config,
                                      Extensions::DynamicModules::DynamicModulePtr dynamic_module);

} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
