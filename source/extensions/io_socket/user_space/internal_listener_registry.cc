#include "source/extensions/io_socket/user_space/internal_listener_registry.h"

#include "envoy/singleton/manager.h"

#include "source/common/singleton/threadsafe_singleton.h"
#include "source/extensions/io_socket/user_space/client_connection_factory.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

SINGLETON_MANAGER_REGISTRATION(internal_listener_registry);

void InternalListenerExtension::onServerInitialized() {
  ENVOY_LOG_MISC(debug, "lambdai: InternalListenerExtension::onServerInitialized");
  std::shared_ptr<TlsInternalListenerRegistry> internal_listener_registry =
      server_context_.singletonManager().getTyped<TlsInternalListenerRegistry>(
          SINGLETON_MANAGER_REGISTERED_NAME(internal_listener_registry),
          [registry = tls_registry_]() { return registry; });
  // Save it in the singleton so the listener manager can obtain during a listener config update.
  ASSERT(internal_listener_registry == tls_registry_);
  ASSERT(internal_listener_registry->tls_slot_ == nullptr);

  tls_registry_->tls_slot_ =
      ThreadLocal::TypedSlot<Extensions::InternalListener::ThreadLocalRegistryImpl>::makeUnique(
          server_context_.threadLocal());
  tls_registry_->tls_slot_->set([](Event::Dispatcher&) {
    return std::make_shared<Extensions::InternalListener::ThreadLocalRegistryImpl>();
  });

  Extensions::IoSocket::UserSpace::InternalClientConnectionFactory::registry_tls_slot_ =
      tls_registry_->tls_slot_.get();
}

Server::BootstrapExtensionPtr InternalListenerRegistryFactory::createBootstrapExtension(
    const Protobuf::Message&, Server::Configuration::ServerFactoryContext& context) {
  return std::make_unique<InternalListenerExtension>(context);
}

REGISTER_FACTORY(InternalListenerRegistryFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
