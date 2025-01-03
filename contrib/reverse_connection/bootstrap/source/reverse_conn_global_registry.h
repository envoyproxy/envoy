#pragma once

#include "envoy/config/typed_config.h"
#include "contrib/envoy/extensions/bootstrap/reverse_connection/v3alpha/reverse_connection.pb.h"
#include "contrib/envoy/extensions/bootstrap/reverse_connection/v3alpha/reverse_connection.pb.validate.h"
#include "contrib/envoy/extensions/reverse_connection/reverse_connection_listener_config/v3alpha/reverse_connection_listener_config.pb.h"
#include "contrib/envoy/extensions/reverse_connection/reverse_connection_listener_config/v3alpha/reverse_connection_listener_config.pb.validate.h"
#include "envoy/network/connection_handler.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "contrib/reverse_connection/bootstrap/source/reverse_conn_thread_local_registry.h"
#include "contrib/reverse_connection/reverse_connection_listener_config/source/reverse_connection_listener_config_impl.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// This ReverseConnRegistry implementation owns a thread local slot.
class ReverseConnRegistry : public Singleton::Instance,
                            public Network::RevConnRegistry,
                            public Logger::Loggable<Logger::Id::main> {
public:
  Bootstrap::ReverseConnection::RCThreadLocalRegistry* getLocalRegistry() override;

  absl::StatusOr<Network::ReverseConnectionListenerConfigPtr> fromAnyConfig(
      const google::protobuf::Any& config) override;

  std::unique_ptr<ThreadLocal::TypedSlot<Bootstrap::ReverseConnection::RCThreadLocalRegistry>>
      tls_slot_;
};

class ReverseConnExtension : public Server::BootstrapExtension,
                             public Logger::Loggable<Logger::Id::main> {
public:
  ReverseConnExtension(
      Server::Configuration::ServerFactoryContext& server_context,
      const envoy::extensions::bootstrap::reverse_connection::v3alpha::ReverseConnection& config);

  ~ReverseConnExtension() override = default;
  void onServerInitialized() override;

private:
  Server::Configuration::ServerFactoryContext& server_context_;
  std::shared_ptr<ReverseConnRegistry> global_tls_registry_;
  std::string stat_prefix_;
};

// The factory creates the `ReverseConnExtension` instance when envoy starts.
class ReverseConnExtensionFactory : public Server::Configuration::BootstrapExtensionFactory,
                                    public Logger::Loggable<Logger::Id::main> {
public:
  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::bootstrap::reverse_connection::v3alpha::ReverseConnection>();
  }
  std::string name() const override { return "envoy.bootstrap.reverse_connection"; };
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
