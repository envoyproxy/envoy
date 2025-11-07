#pragma once

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

#ifndef KAE_DISABLED
#include "contrib/kae/private_key_providers/source/kae_private_key_provider.h"
#endif

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Kae {

class KaePrivateKeyMethodFactory : public Ssl::PrivateKeyMethodProviderInstanceFactory {
  // Ssl::PrivateKeyMethodProviderInstanceFactory
  Ssl::PrivateKeyMethodProviderSharedPtr createPrivateKeyMethodProviderInstance(
      const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& message,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) override;

public:
  std::string name() const override { return "kae"; };
};

} // namespace Kae
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
