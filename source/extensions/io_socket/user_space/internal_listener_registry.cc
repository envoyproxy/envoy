#include "source/extensions/io_socket/user_space/internal_listener_registry.h"

#include "envoy/singleton/manager.h"

#include "source/common/singleton/threadsafe_singleton.h"
#include "source/extensions/io_socket/user_space/client_connection_factory.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {

SINGLETON_MANAGER_REGISTRATION(internal_listener_registry);

InternalListenerExtension::InternalListenerExtension(
    Server::Configuration::ServerFactoryContext& server_context)
    : server_context_(server_context),
      tls_registry_(std::make_shared<TlsInternalListenerRegistry>()) {
  // Initialize this singleton before the listener manager potentially load a static internal
  // listener.
  server_context_.singletonManager().getTyped<TlsInternalListenerRegistry>(
      SINGLETON_MANAGER_REGISTERED_NAME(internal_listener_registry),
      [registry = tls_registry_]() { return registry; });
}

void InternalListenerExtension::onServerInitialized() {
  std::shared_ptr<TlsInternalListenerRegistry> internal_listener_registry =
      server_context_.singletonManager().getTyped<TlsInternalListenerRegistry>(
          SINGLETON_MANAGER_REGISTERED_NAME(internal_listener_registry));

  // Save it in the singleton so the listener manager can obtain during a listener config update.
  ASSERT(internal_listener_registry == tls_registry_);
  ASSERT(internal_listener_registry->tls_slot_ == nullptr);

  tls_registry_->tls_slot_ =
      ThreadLocal::TypedSlot<IoSocket::UserSpace::ThreadLocalRegistryImpl>::makeUnique(
          server_context_.threadLocal());
  tls_registry_->tls_slot_->set([](Event::Dispatcher&) {
    return std::make_shared<IoSocket::UserSpace::ThreadLocalRegistryImpl>();
  });

  // Now the thread local registry is available. This thread local object is published to
  // ``InternalClientConnectionFactory``.
  // Note that the per silo ``ConnectionHandler`` will add internal listeners into the per silo
  // registry.
  Extensions::IoSocket::UserSpace::InternalClientConnectionFactory::registry_tls_slot_ =
      tls_registry_->tls_slot_.get();
}

Server::BootstrapExtensionPtr InternalListenerRegistryFactory::createBootstrapExtension(
    [[maybe_unused]] const Protobuf::Message& config,
    Server::Configuration::ServerFactoryContext& context) {
  return std::make_unique<InternalListenerExtension>(context);
}

REGISTER_FACTORY(InternalListenerRegistryFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
