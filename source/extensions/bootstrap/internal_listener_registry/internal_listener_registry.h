#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/extensions/bootstrap/internal_listener_registry/v3/internal_listener_registry.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "source/extensions/bootstrap/internal_listener_registry/thread_local_registry.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListenerRegistry {

// This InternalListenerRegistry implementation owns a thread local slot.
class TlsInternalListenerRegistry : public Singleton::Instance,
                                    public Network::InternalListenerRegistry {
public:
  Network::LocalInternalListenerRegistry* getLocalRegistry() override {
    if (auto opt = tls_slot_->get(); opt.has_value()) {
      return &opt.value().get();
    }
    return nullptr;
  }

  std::unique_ptr<
      ThreadLocal::TypedSlot<Bootstrap::InternalListenerRegistry::ThreadLocalRegistryImpl>>
      tls_slot_;
};

// This extension maintains the life of the ``TlsInternalListenerRegistry`` singleton.
// The ``TlsInternalListenerRegistry`` is functionally ready after the Envoy thread local system is
// initialized.
class InternalListenerExtension : public Server::BootstrapExtension {
public:
  explicit InternalListenerExtension(Server::Configuration::ServerFactoryContext& server_context);

  ~InternalListenerExtension() override = default;

  // Server::Configuration::BootstrapExtension
  void onServerInitialized() override;

  Server::Configuration::ServerFactoryContext& server_context_;
  std::shared_ptr<TlsInternalListenerRegistry> tls_registry_;
};

// The factory creates the `InternalListenerExtension` instance when envoy starts.
class InternalListenerRegistryFactory : public Server::Configuration::BootstrapExtensionFactory {
public:
  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::bootstrap::internal_listener_registry::v3::InternalListenerRegistry>();
  }
  std::string name() const override { return "envoy.bootstrap.internal_listener_registry"; };
};

} // namespace InternalListenerRegistry
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
