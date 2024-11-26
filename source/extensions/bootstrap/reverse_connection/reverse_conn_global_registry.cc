#include "source/extensions/bootstrap/reverse_connection/reverse_conn_global_registry.h"

#include "envoy/event/dispatcher.h"
#include "envoy/singleton/manager.h"

#include "source/common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

SINGLETON_MANAGER_REGISTRATION(reverse_conn_registry);

ReverseConnExtension::ReverseConnExtension(
    Server::Configuration::ServerFactoryContext& server_context,
    const envoy::extensions::bootstrap::reverse_connection::v3::ReverseConnection& config)
    : server_context_(server_context),
      global_tls_registry_(std::make_shared<ReverseConnRegistry>()) {
  stat_prefix_ = PROTOBUF_GET_STRING_OR_DEFAULT(config, stat_prefix, "reverse_connection");
  ENVOY_LOG_MISC(debug,
                 "ReverseConnExtension: creating reverse connection registry with stat_prefix: {}",
                 stat_prefix_);
  server_context_.singletonManager().getTyped<ReverseConnRegistry>(
      SINGLETON_MANAGER_REGISTERED_NAME(reverse_conn_registry),
      [registry = global_tls_registry_]() { return registry; });
}

void ReverseConnExtension::onServerInitialized() {
  std::shared_ptr<ReverseConnRegistry> reverse_conn_registry =
      server_context_.singletonManager().getTyped<ReverseConnRegistry>(
          SINGLETON_MANAGER_REGISTERED_NAME(reverse_conn_registry));
  ASSERT(reverse_conn_registry == global_tls_registry_);
  ASSERT(reverse_conn_registry->tls_slot_ == nullptr);
  ENVOY_LOG_MISC(debug, "ReverseConnExtension: creating reverse connection tls slot");
  global_tls_registry_->tls_slot_ =
      ThreadLocal::TypedSlot<Bootstrap::ReverseConnection::RCThreadLocalRegistry>::makeUnique(
          server_context_.threadLocal());
  Stats::Scope& scope = server_context_.scope();
  global_tls_registry_->tls_slot_->set([this, &scope](Event::Dispatcher& dispatcher) {
    return std::make_shared<Bootstrap::ReverseConnection::RCThreadLocalRegistry>(dispatcher, scope,
                                                                                 stat_prefix_);
  });
}

Server::BootstrapExtensionPtr ReverseConnExtensionFactory::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  ENVOY_LOG_MISC(debug,
                 "ReverseConnListenerFactory: creating reverse conn extension with config: {}",
                 config.DebugString());
  const auto& message = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::reverse_connection::v3::ReverseConnection&>(
      config, context.messageValidationVisitor());
  return std::make_unique<ReverseConnExtension>(context, message);
}

REGISTER_FACTORY(ReverseConnExtensionFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
