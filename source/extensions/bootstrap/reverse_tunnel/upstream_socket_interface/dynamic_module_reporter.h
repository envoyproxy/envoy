#pragma once

#include "envoy/extensions/bootstrap/reverse_tunnel/reporter/dynamic_modules/v3/dynamic_module_reporter.pb.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/reverse_tunnel_reporter.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Type aliases for the five ABI function pointers resolved from the module.
using OnReverseTunnelReporterNewType =
    decltype(&envoy_dynamic_module_on_reverse_tunnel_reporter_new);
using OnReverseTunnelReporterDestroyType =
    decltype(&envoy_dynamic_module_on_reverse_tunnel_reporter_destroy);
using OnReverseTunnelServerInitializedType =
    decltype(&envoy_dynamic_module_on_reverse_tunnel_server_initialized);
using OnReverseTunnelConnectedType = decltype(&envoy_dynamic_module_on_reverse_tunnel_connected);
using OnReverseTunnelDisconnectedType =
    decltype(&envoy_dynamic_module_on_reverse_tunnel_disconnected);

/**
 * A ReverseTunnelReporter backed by a dynamic module. The module is loaded via dlopen and the
 * five ABI hooks are resolved at construction time. The in-module reporter object is created via
 * the reporter_new hook and destroyed via reporter_destroy.
 */
class DynamicModuleReverseTunnelReporter : public ReverseTunnelReporter {
public:
  DynamicModuleReverseTunnelReporter(Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                     OnReverseTunnelReporterNewType on_reporter_new,
                                     OnReverseTunnelReporterDestroyType on_reporter_destroy,
                                     OnReverseTunnelServerInitializedType on_server_initialized,
                                     OnReverseTunnelConnectedType on_connected,
                                     OnReverseTunnelDisconnectedType on_disconnected,
                                     absl::string_view reporter_config);

  ~DynamicModuleReverseTunnelReporter() override;

  // ReverseTunnelReporter
  void onServerInitialized() override;
  void reportConnectionEvent(absl::string_view node_id, absl::string_view cluster_id,
                             absl::string_view tenant_id) override;
  void reportDisconnectionEvent(absl::string_view node_id, absl::string_view cluster_id) override;

private:
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
  envoy_dynamic_module_type_reverse_tunnel_reporter_module_ptr in_module_reporter_{nullptr};

  OnReverseTunnelReporterDestroyType on_reporter_destroy_{nullptr};
  OnReverseTunnelServerInitializedType on_server_initialized_{nullptr};
  OnReverseTunnelConnectedType on_connected_{nullptr};
  OnReverseTunnelDisconnectedType on_disconnected_{nullptr};
};

/**
 * Factory that creates DynamicModuleReverseTunnelReporter instances.
 * Registered under the name "envoy.extensions.reverse_tunnel.reporting_service.dynamic_modules".
 */
class DynamicModuleReverseTunnelReporterFactory : public ReverseTunnelReporterFactory {
public:
  ReverseTunnelReporterPtr createReporter(Server::Configuration::ServerFactoryContext& context,
                                          ProtobufTypes::MessagePtr message) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override {
    return "envoy.extensions.reverse_tunnel.reporting_service.dynamic_modules";
  }
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
