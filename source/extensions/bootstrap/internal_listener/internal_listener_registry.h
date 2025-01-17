#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/extensions/bootstrap/internal_listener/v3/internal_listener.pb.h"
#include "envoy/extensions/bootstrap/internal_listener/v3/internal_listener.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "source/extensions/bootstrap/internal_listener/thread_local_registry.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

// This InternalListenerRegistry implementation owns a thread local slot.
class TlsInternalListenerRegistry : public Singleton::Instance,
                                    public Network::InternalListenerRegistry {
public:
  Network::LocalInternalListenerRegistry* getLocalRegistry() override {
    // The tls slot may not initialized yet. This can happen in production when Envoy is starting.
    if (!tls_slot_) {
      return nullptr;
    }
    if (auto opt = tls_slot_->get(); opt.has_value()) {
      return &opt.value().get();
    }
    return nullptr;
  }

  std::unique_ptr<ThreadLocal::TypedSlot<Bootstrap::InternalListener::ThreadLocalRegistryImpl>>
      tls_slot_;
};

// This extension maintains the life of the ``TlsInternalListenerRegistry`` singleton.
// The ``TlsInternalListenerRegistry`` is functionally ready after the Envoy thread local system is
// initialized.
class InternalListenerExtension : public Server::BootstrapExtension {
public:
  InternalListenerExtension(
      Server::Configuration::ServerFactoryContext& server_context,
      const envoy::extensions::bootstrap::internal_listener::v3::InternalListener& config);

  ~InternalListenerExtension() override = default;

  // Server::Configuration::BootstrapExtension
  void onServerInitialized() override;

private:
  Server::Configuration::ServerFactoryContext& server_context_;
  std::shared_ptr<TlsInternalListenerRegistry> tls_registry_;
  uint32_t buffer_size_;
};

// The factory creates the `InternalListenerExtension` instance when envoy starts.
class InternalListenerFactory : public Server::Configuration::BootstrapExtensionFactory {
public:
  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::bootstrap::internal_listener::v3::InternalListener>();
  }
  std::string name() const override { return "envoy.bootstrap.internal_listener"; };
};

} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
