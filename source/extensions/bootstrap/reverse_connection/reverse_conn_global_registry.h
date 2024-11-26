#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/extensions/bootstrap/reverse_connection/v3/reverse_connection.pb.h"
#include "envoy/extensions/bootstrap/reverse_connection/v3/reverse_connection.pb.validate.h"
#include "envoy/network/connection_handler.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "source/extensions/bootstrap/reverse_connection/reverse_conn_thread_local_registry.h"

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
  Network::LocalRevConnRegistry* getLocalRegistry() override {
    // The tls slot may not initialized yet. This can happen in production when Envoy is starting.
    if (!tls_slot_) {
      return nullptr;
    }
    if (auto opt = tls_slot_->get(); opt.has_value()) {
      return &opt.value().get();
    }
    return nullptr;
  }

  std::unique_ptr<ThreadLocal::TypedSlot<Bootstrap::ReverseConnection::RCThreadLocalRegistry>>
      tls_slot_;
};

class ReverseConnExtension : public Server::BootstrapExtension,
                             public Logger::Loggable<Logger::Id::main> {
public:
  ReverseConnExtension(
      Server::Configuration::ServerFactoryContext& server_context,
      const envoy::extensions::bootstrap::reverse_connection::v3::ReverseConnection& config);

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
        envoy::extensions::bootstrap::reverse_connection::v3::ReverseConnection>();
  }
  std::string name() const override { return "envoy.bootstrap.reverse_connection"; };
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
