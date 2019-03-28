#include "extensions/transport_sockets/tls/private_key/private_key_manager_impl.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

Envoy::Ssl::PrivateKeyOperationsProviderSharedPtr
PrivateKeyOperationsManagerImpl::createPrivateKeyOperationsProvider(
    const envoy::api::v2::auth::PrivateKeyOperations& message,
    Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) {

  Ssl::PrivateKeyOperationsProviderInstanceFactory* factory =
      Registry::FactoryRegistry<Ssl::PrivateKeyOperationsProviderInstanceFactory>::getFactory(
          message.provider_name());

  // Create a new provider instance with the configuration.
  if (factory) {
    return factory->createPrivateKeyOperationsProviderInstance(message,
                                                               private_key_provider_context);
  }

  return nullptr;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
