#include "source/common/tls/private_key/private_key_manager_impl.h"

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

Envoy::Ssl::PrivateKeyMethodProviderSharedPtr
PrivateKeyMethodManagerImpl::createPrivateKeyMethodProvider(
    const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context) {

  Ssl::PrivateKeyMethodProviderInstanceFactory* factory =
      Registry::FactoryRegistry<Ssl::PrivateKeyMethodProviderInstanceFactory>::getFactory(
          config.provider_name());

  // Create a new provider instance with the configuration.
  if (factory) {
    return factory->createPrivateKeyMethodProviderInstance(config, factory_context);
  }

  return nullptr;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
