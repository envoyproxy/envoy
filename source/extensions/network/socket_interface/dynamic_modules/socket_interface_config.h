#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

using OnSocketInterfaceConfigNewType =
    decltype(&envoy_dynamic_module_on_socket_interface_config_new);
using OnSocketInterfaceConfigDestroyType =
    decltype(&envoy_dynamic_module_on_socket_interface_config_destroy);
using OnSocketInterfaceIpFamilySupportedType =
    decltype(&envoy_dynamic_module_on_socket_interface_ip_family_supported);
using OnSocketInterfaceSocketType = decltype(&envoy_dynamic_module_on_socket_interface_socket);
using OnSocketInterfaceSocketCloseType =
    decltype(&envoy_dynamic_module_on_socket_interface_socket_close);
using OnSocketInterfaceSocketDuplicateType =
    decltype(&envoy_dynamic_module_on_socket_interface_socket_duplicate);
using OnSocketInterfaceSetSocketHandleType =
    decltype(&envoy_dynamic_module_on_socket_interface_set_socket_handle);
using OnSocketInterfaceIoReadType = decltype(&envoy_dynamic_module_on_socket_interface_io_read);
using OnSocketInterfaceIoWriteType = decltype(&envoy_dynamic_module_on_socket_interface_io_write);
using OnSocketInterfaceIoShutdownType =
    decltype(&envoy_dynamic_module_on_socket_interface_io_shutdown);
using OnSocketInterfaceAcceptType = decltype(&envoy_dynamic_module_on_socket_interface_accept);

#define DYNAMIC_MODULE_SOCKET_INTERFACE_STATS(COUNTER)                                             \
  COUNTER(socket_failed)                                                                           \
  COUNTER(accept_failed)                                                                           \
  COUNTER(duplicate_failed)

/**
 * Counters of the socket operations the module fails, under
 * dynamic_modules.socket_interface.<socket_interface_name>.
 */
struct DynamicModuleSocketInterfaceStats {
  DYNAMIC_MODULE_SOCKET_INTERFACE_STATS(GENERATE_COUNTER_STRUCT)
};

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
                                     Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                     Stats::Scope& scope);

  ~DynamicModuleSocketInterfaceConfig();

  // Whether the module implements the I/O hooks, which a socket needs to set DelegateIo.
  bool implementsIoHooks() const { return on_io_read_ != nullptr; }
  // Whether the module implements the accept hook, which a socket needs to set
  // HandsOverConnections.
  bool implementsAccept() const { return on_accept_ != nullptr; }

  const std::string& socketInterfaceName() const { return socket_interface_name_; }
  DynamicModuleSocketInterfaceStats& stats() { return stats_; }

  // A hot-restarted Envoy adopts its parent's listen sockets as plain file descriptors, without
  // the module state their I/O or accepts depend on, so sockets set flags only without it.
  bool hotRestartDisabled() const { return hot_restart_disabled_; }
  void setHotRestartDisabled(bool disabled) { hot_restart_disabled_ = disabled; }

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_socket_interface_config_module_ptr in_module_config_{nullptr};

  // The required hooks are never null. The I/O hooks are all set or all null, and
  // set_socket_handle and socket_duplicate are set whenever the I/O hooks or accept are.
  OnSocketInterfaceConfigDestroyType on_config_destroy_{nullptr};
  OnSocketInterfaceIpFamilySupportedType on_ip_family_supported_{nullptr};
  OnSocketInterfaceSocketType on_socket_{nullptr};
  OnSocketInterfaceSocketCloseType on_socket_close_{nullptr};
  OnSocketInterfaceSocketDuplicateType on_socket_duplicate_{nullptr};
  OnSocketInterfaceSetSocketHandleType on_set_socket_handle_{nullptr};
  OnSocketInterfaceIoReadType on_io_read_{nullptr};
  OnSocketInterfaceIoWriteType on_io_write_{nullptr};
  OnSocketInterfaceIoShutdownType on_io_shutdown_{nullptr};
  OnSocketInterfaceAcceptType on_accept_{nullptr};

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleSocketInterfaceConfig>>
  newDynamicModuleSocketInterfaceConfig(absl::string_view socket_interface_name,
                                        absl::string_view socket_interface_config,
                                        Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                        Stats::Scope& scope);

  const std::string socket_interface_name_;
  const std::string socket_interface_config_;
  DynamicModuleSocketInterfaceStats stats_;
  bool hot_restart_disabled_{false};
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleSocketInterfaceConfigSharedPtr =
    std::shared_ptr<DynamicModuleSocketInterfaceConfig>;

/**
 * Creates a new DynamicModuleSocketInterfaceConfig for the given configuration.
 * @param socket_interface_name the name identifying the implementation within the module.
 * @param socket_interface_config the configuration bytes for the implementation.
 * @param dynamic_module the dynamic module to use.
 * @param scope the scope the interface's counters are created in.
 * @return a shared pointer to the new config object or an error if symbol resolution failed.
 */
absl::StatusOr<DynamicModuleSocketInterfaceConfigSharedPtr> newDynamicModuleSocketInterfaceConfig(
    absl::string_view socket_interface_name, absl::string_view socket_interface_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& scope);

} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
