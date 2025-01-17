#include "source/extensions/bootstrap/internal_listener/internal_listener_registry.h"

#include "envoy/singleton/manager.h"

#include "source/common/singleton/threadsafe_singleton.h"
#include "source/extensions/bootstrap/internal_listener/client_connection_factory.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

SINGLETON_MANAGER_REGISTRATION(internal_listener_registry);

InternalListenerExtension::InternalListenerExtension(
    Server::Configuration::ServerFactoryContext& server_context,
    const envoy::extensions::bootstrap::internal_listener::v3::InternalListener& config)
    : server_context_(server_context),
      tls_registry_(std::make_shared<TlsInternalListenerRegistry>()) {
  // Get buffer size config in K bytes. The default buffer size is 1024 KiB.
  buffer_size_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
                     config, buffer_size_kb, InternalClientConnectionFactory::DefaultBufferSize) *
                 1024;

  // Initialize this singleton before the listener manager potentially load a static internal
  // listener.
  server_context_.singletonManager().getTyped<TlsInternalListenerRegistry>(
      SINGLETON_MANAGER_REGISTERED_NAME(internal_listener_registry),
      [registry = tls_registry_]() { return registry; });
}

void InternalListenerExtension::onServerInitialized() {
  std::shared_ptr<TlsInternalListenerRegistry> internal_listener =
      server_context_.singletonManager().getTyped<TlsInternalListenerRegistry>(
          SINGLETON_MANAGER_REGISTERED_NAME(internal_listener_registry));

  // Save it in the singleton so the listener manager can obtain during a listener config update.
  ASSERT(internal_listener == tls_registry_);
  ASSERT(internal_listener->tls_slot_ == nullptr);

  tls_registry_->tls_slot_ =
      ThreadLocal::TypedSlot<Bootstrap::InternalListener::ThreadLocalRegistryImpl>::makeUnique(
          server_context_.threadLocal());
  tls_registry_->tls_slot_->set([](Event::Dispatcher&) {
    return std::make_shared<Bootstrap::InternalListener::ThreadLocalRegistryImpl>();
  });

  // Now the thread local registry is available. This thread local object is published to
  // ``InternalClientConnectionFactory``.
  // Note that the per silo ``ConnectionHandler`` will add internal listeners into the per silo
  // registry.
  InternalClientConnectionFactory::registry_tls_slot_ = tls_registry_->tls_slot_.get();
  InternalClientConnectionFactory::buffer_size_ = buffer_size_;
}

Server::BootstrapExtensionPtr InternalListenerFactory::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& message = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::internal_listener::v3::InternalListener&>(
      config, context.messageValidationVisitor());
  return std::make_unique<InternalListenerExtension>(context, message);
}

REGISTER_FACTORY(InternalListenerFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
