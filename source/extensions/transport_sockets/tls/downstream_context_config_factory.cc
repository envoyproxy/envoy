#include "extensions/transport_sockets/tls/downstream_context_config_factory.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

Ssl::ContextConfigPtr DownstreamContextConfigFactory::createSslContextConfig(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  return std::make_unique<ServerContextConfigImpl>(
      dynamic_cast<const envoy::api::v2::auth::DownstreamTlsContext&>(config), context);
}

REGISTER_FACTORY(DownstreamContextConfigFactory, Ssl::ContextConfigFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
