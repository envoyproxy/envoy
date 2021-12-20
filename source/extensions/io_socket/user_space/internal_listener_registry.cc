#include "source/extensions/io_socket/user_space/internal_listener_registry.h"

#include "source/extensions/io_socket/user_space/client_connection_factory.h"
#include "envoy/singleton/manager.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

void InternalListenerExtension::onServerInitialized() {

  std::shared_ptr<TlsInternalListenerRegistry> internal_listener_registry =
      server_context_.singletonManager().getTyped<TlsInternalListenerRegistry>(
          SINGLETON_MANAGER_REGISTERED_NAME(internal_listener_registry),
          []() { return std::make_shared<TlsInternalListenerRegistry>(); });

  ASSERT(internal_listener_registry->tls_slot_ == nullptr);

  internal_listener_registry->tls_slot_ =
      ThreadLocal::TypedSlot<Extensions::InternalListener::ThreadLocalRegistryImpl>::makeUnique(
          server_context_.threadLocal());
  internal_listener_registry->tls_slot_->set([](Event::Dispatcher&) {
    return std::make_shared<Extensions::InternalListener::ThreadLocalRegistryImpl>();
  });
  ASSERT(Extensions::IoSocket::UserSpace::InternalClientConnectionFactory::registry_tls_slot_ ==
         nullptr);
  Extensions::IoSocket::UserSpace::InternalClientConnectionFactory::registry_tls_slot_ =
      internal_listener_registry->tls_slot_.get();
}

Server::BootstrapExtensionPtr InternalListenerRegistryFactory::createBootstrapExtension(
    const Protobuf::Message&, Server::Configuration::ServerFactoryContext& context) {
  return std::make_unique<InternalListenerExtension>(context);
}

} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
